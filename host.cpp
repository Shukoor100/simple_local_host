// Simple HTTP server in C++ that serves index.html and saves form messages to msg.txt
// Works on Windows, Linux and macOS.

#ifdef _WIN32
    #define _WIN32_WINNT 0x0600
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define CLOSE_SOCKET close
#endif

#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

const int PORT = 8000;

// Find this computer's IP on the local network (same UDP trick as the Python version)
std::string get_local_ip() {
    std::string result = "127.0.0.1";
    socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return result;

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    if (connect(s, (sockaddr*)&remote, sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(s, (sockaddr*)&local, &len) == 0) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
            result = buf;
        }
    }
    CLOSE_SOCKET(s);
    return result;
}

// Turn "hi+there%21" into "hi there!"
std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() &&
                   std::isxdigit((unsigned char)s[i + 1]) &&
                   std::isxdigit((unsigned char)s[i + 2])) {
            out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Look for name=value in a query string like "message=hello&x=1"
bool get_query_param(const std::string& query, const std::string& name, std::string& value) {
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        if (pair.rfind(name + "=", 0) == 0) {          // pair starts with "name="
            value = url_decode(pair.substr(name.size() + 1));
            return true;
        }
    }
    return false;
}

// send() may not send everything at once, so keep going until it's all out
void send_all(socket_t sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(sock, data.c_str() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) return;
        sent += n;
    }
}

// Build a full HTTP response by hand and send it
void send_response(socket_t sock, const std::string& status, const std::string& content_type,
                   const std::string& body, const std::string& extra_headers = "") {
    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: " + content_type + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n" +
        extra_headers +
        "\r\n" +                                     // blank line = end of headers
        body;
    send_all(sock, response);
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    // 1. Create a TCP socket
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    // Let us restart the server right away without "address already in use"
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // 2. Bind it to 0.0.0.0:8000
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 0.0.0.0
    addr.sin_port = htons(PORT);

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "Could not bind to port " << PORT << " (is it already in use?)\n";
        return 1;
    }

    // 3. Start listening for connections
    listen(server, 10);

    std::cout << "Server running!\n"
              << "  On this computer:  http://localhost:" << PORT << "\n"
              << "  On your network:   http://" << get_local_ip() << ":" << PORT << "\n"
              << "Press Ctrl+C to stop.\n";

    // 4. Handle one connection at a time, forever
    while (true) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        socket_t conn = accept(server, (sockaddr*)&client, &client_len);
        if (conn == INVALID_SOCKET) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));

        // Read the raw request text from the browser
        char buf[8192];
        int n = recv(conn, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            CLOSE_SOCKET(conn);
            continue;
        }
        buf[n] = '\0';

        // First line looks like: GET /?message=hello HTTP/1.1
        std::istringstream request(buf);
        std::string method, target, version;
        request >> method >> target >> version;

        // Split "/?message=hello" into path "/" and query "message=hello"
        std::string path = target, query;
        size_t q = target.find('?');
        if (q != std::string::npos) {
            path = target.substr(0, q);
            query = target.substr(q + 1);
        }

        std::string status;
        std::string msg;

        if (method != "GET") {
            status = "405 Method Not Allowed";
            send_response(conn, status, "text/plain", "Only GET is supported\n");
        } else if (get_query_param(query, "message", msg)) {
            // Keep each message on one line in the file
            for (char& c : msg) if (c == '\n' || c == '\r') c = ' ';

            if (!msg.empty()) {
                std::ofstream file("msg.txt", std::ios::app);
                file << "[" << timestamp() << "] " << ip << ": " << msg << "\n";
                std::cout << "Saved message: " << msg << "\n";
            }
            status = "303 See Other";
            send_response(conn, status, "text/plain", "", "Location: /\r\n");
        } else if (path == "/" || path == "/index.html") {
            std::string html;
            if (read_file("index.html", html)) {
                status = "200 OK";
                send_response(conn, status, "text/html; charset=utf-8", html);
            } else {
                status = "404 Not Found";
                send_response(conn, status, "text/plain", "index.html not found\n");
            }
        } else {
            status = "404 Not Found";
            send_response(conn, status, "text/plain", "Not found\n");
        }

        std::cout << ip << " -> " << method << " " << target << " " << status << "\n";
        CLOSE_SOCKET(conn);
    }

    CLOSE_SOCKET(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}