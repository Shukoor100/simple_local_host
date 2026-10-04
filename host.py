import socket
from datetime import datetime
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HOST = "0.0.0.0"  # listen on all network interfaces so other devices can connect
PORT = 8000


def get_local_ip():
    """Find this computer's IP address on the local network."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


class MyHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)          # split "/?message=hello" into parts
        query = parse_qs(parsed.query)        # {"message": ["hello"]}

        if "message" in query:
            msg = query["message"][0].strip()
            if msg:
                time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                with open("msg.txt", "a", encoding="utf-8") as f:
                    f.write(f"[{time}] {self.client_address[0]}: {msg}\n")
                print(f"Saved message: {msg}")

            # Send the browser back to the clean home page
            self.send_response(303)
            self.send_header("Location", "/")
            self.end_headers()
            return

        if parsed.path == "/":
            self.path = "/index.html"
        return super().do_GET()

    def log_message(self, format, *args):
        print(f"{self.client_address[0]} -> {format % args}")


if __name__ == "__main__":
    server = HTTPServer((HOST, PORT), MyHandler)
    ip = get_local_ip()
    print("Server running!")
    print(f"  On this computer:  http://localhost:{PORT}")
    print(f"  On your network:   http://{ip}:{PORT}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.server_close()