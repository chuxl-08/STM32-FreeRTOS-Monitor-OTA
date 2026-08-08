#!/usr/bin/env python3
"""Single-port Upload/OTA test server for STM32_Monitor ESP01 validation."""

from __future__ import annotations

import argparse
import socket
import socketserver
import tempfile
import threading
from pathlib import Path
from urllib.parse import unquote, urlsplit


HTTP_CHUNK_SIZE = 4096
DEFAULT_IDLE_TIMEOUT_S = 600.0


class OtaTriggerServer(socketserver.ThreadingTCPServer):
    """TCP server that handles both raw upload payloads and HTTP package GETs."""

    allow_reuse_address = True
    daemon_threads = True

    def __init__(
        self,
        server_address: tuple[str, int],
        handler_class: type[socketserver.BaseRequestHandler],
        *,
        root: Path,
        ota_count: int,
        ota_path: str | None,
        ota_version: int | None,
        idle_timeout_s: float,
        print_upload_payload: bool,
    ) -> None:
        super().__init__(server_address, handler_class)
        self.root = root.resolve()
        self.ota_count = ota_count
        self.ota_path = ota_path
        self.ota_version = ota_version
        self.idle_timeout_s = idle_timeout_s
        self.print_upload_payload = print_upload_payload
        self._ota_lock = threading.Lock()

    def consume_ota_request(self) -> bool:
        """Return True if this upload should receive an OTA=1 command."""
        with self._ota_lock:
            if self.ota_count < 0:
                return True
            if self.ota_count > 0:
                self.ota_count -= 1
                return True
            return False

    def build_ota_command(self) -> bytes:
        """Build the OTA command returned to UploadTask."""
        parts = ["OTA=1"]
        if self.ota_path:
            parts.append(f"PATH={self.ota_path}")
        if self.ota_version is not None:
            parts.append(f"VER={self.ota_version}")
        return (";".join(parts) + "\r\n").encode("ascii")


class OtaTriggerHandler(socketserver.BaseRequestHandler):
    """Route ESP01 connections to raw upload handling or HTTP package serving."""

    server: OtaTriggerServer

    def handle(self) -> None:
        self.request.settimeout(self.server.idle_timeout_s)
        try:
            first_data = self.request.recv(HTTP_CHUNK_SIZE)
        except (socket.timeout, ConnectionResetError):
            return

        if not first_data:
            return

        if first_data.startswith(b"GET "):
            self._handle_http_get(first_data)
        else:
            self._handle_upload_stream(first_data)

    def _handle_upload_stream(self, first_data: bytes) -> None:
        """Reply to each raw UploadTask payload with OTA=1 or OK."""
        data = first_data
        while data:
            response = self.server.build_ota_command() if self.server.consume_ota_request() else b"OK\r\n"
            print(
                f"[UPLOAD] {self.client_address[0]}:{self.client_address[1]} "
                f"len={len(data)} reply={response.decode().strip()}"
            )
            if self.server.print_upload_payload:
                self._print_upload_payload(data)
            try:
                self.request.sendall(response)
            except (BrokenPipeError, ConnectionResetError):
                return

            try:
                data = self.request.recv(HTTP_CHUNK_SIZE)
            except (socket.timeout, ConnectionResetError):
                return

    def _print_upload_payload(self, data: bytes) -> None:
        """Print raw UploadTask payload and a compact OTA report summary."""
        text = data.decode("ascii", errors="replace").replace("\r\n", "\n").strip()
        if not text:
            print("[UPLOAD][PAYLOAD] <empty>")
            return

        fields = self._parse_payload_fields(text)
        report_keys = [
            "OTA_STATE",
            "OTA_CONFIRMED",
            "OTA_PENDING",
            "OTA_BOOT",
            "OTA_SLOT_A_VER",
            "OTA_SLOT_B_VER",
            "OTA_LAST",
            "OTA_DL_STATUS",
            "OTA_TARGET",
            "OTA_PATH",
            "OTA_EXPECT_VER",
            "OTA_PKG_VER",
        ]
        report = " ".join(f"{key}={fields[key]}" for key in report_keys if key in fields)
        if report:
            print(f"[UPLOAD][REPORT] {report}")

        print("[UPLOAD][PAYLOAD-BEGIN]")
        print(text)
        print("[UPLOAD][PAYLOAD-END]")

    @staticmethod
    def _parse_payload_fields(text: str) -> dict[str, str]:
        """Parse simple KEY=VALUE fields split by newlines and commas."""
        fields: dict[str, str] = {}
        for line in text.splitlines():
            for part in line.split(","):
                item = part.strip()
                if "=" not in item:
                    continue
                key, value = item.split("=", 1)
                key = key.strip()
                if key:
                    fields[key] = value.strip()
        return fields

    def _handle_http_get(self, first_data: bytes) -> None:
        """Serve a firmware package for ESP01 HTTP GET requests."""
        request_line = first_data.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
        parts = request_line.split()
        if len(parts) < 2:
            self._send_http_error(400, b"bad request\r\n")
            return

        raw_path = unquote(urlsplit(parts[1]).path)
        rel_path = raw_path.lstrip("/")
        if not rel_path or ".." in Path(rel_path).parts:
            self._send_http_error(403, b"forbidden\r\n")
            return

        file_path = (self.server.root / rel_path).resolve()
        if not file_path.is_file() or not self._is_under_root(file_path):
            self._send_http_error(404, b"not found\r\n")
            print(f"[HTTP] {self.client_address[0]}:{self.client_address[1]} 404 {raw_path}")
            return

        size = file_path.stat().st_size
        header = (
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            f"Content-Length: {size}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")

        print(f"[HTTP] {self.client_address[0]}:{self.client_address[1]} 200 /{rel_path} {size} bytes")
        try:
            self.request.sendall(header)
        except (BrokenPipeError, ConnectionResetError):
            return
        with file_path.open("rb") as package:
            while True:
                chunk = package.read(HTTP_CHUNK_SIZE)
                if not chunk:
                    break
                try:
                    self.request.sendall(chunk)
                except (BrokenPipeError, ConnectionResetError):
                    return

    def _send_http_error(self, status: int, body: bytes) -> None:
        reason = {
            400: "Bad Request",
            403: "Forbidden",
            404: "Not Found",
        }.get(status, "Error")
        header = (
            f"HTTP/1.1 {status} {reason}\r\n"
            "Content-Type: text/plain\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii")
        try:
            self.request.sendall(header + body)
        except (BrokenPipeError, ConnectionResetError):
            return

    def _is_under_root(self, file_path: Path) -> bool:
        try:
            file_path.relative_to(self.server.root)
            return True
        except ValueError:
            return False


def run_self_test() -> None:
    """Exercise raw upload command replies and HTTP package serving."""
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        package_path = root / "monitor_slot_b_full_v12.pkg"
        package_path.write_bytes(b"pkg-data")

        server = OtaTriggerServer(
            ("127.0.0.1", 0),
            OtaTriggerHandler,
            root=root,
            ota_count=1,
            ota_path="/monitor_slot_b_full_v12.pkg",
            ota_version=12,
            idle_timeout_s=0.2,
            print_upload_payload=False,
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address

        try:
            with socket.create_connection((host, port), timeout=1.0) as client:
                client.sendall(b"MODE=AUTO_SCAN\r\nOTA_STATE=CONFIRMED\r\n")
                assert client.recv(128) == b"OTA=1;PATH=/monitor_slot_b_full_v12.pkg;VER=12\r\n"
                client.sendall(b"MODE=AUTO_SCAN\r\nOTA_STATE=CONFIRMED\r\n")
                assert client.recv(64) == b"OK\r\n"

            with socket.create_connection((host, port), timeout=1.0) as client:
                client.sendall(
                    b"GET /monitor_slot_b_full_v12.pkg HTTP/1.1\r\n"
                    b"Host: 127.0.0.1\r\n"
                    b"Connection: close\r\n"
                    b"\r\n"
                )
                response = b""
                while True:
                    chunk = client.recv(HTTP_CHUNK_SIZE)
                    if not chunk:
                        break
                    response += chunk
                assert b"HTTP/1.1 200 OK" in response
                assert response.endswith(b"pkg-data")
        finally:
            server.shutdown()
            server.server_close()

    print("self-test OK")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Serve STM32_Monitor OTA packages and reply OTA=1 to raw UploadTask "
            "TCP payloads on the same port."
        )
    )
    parser.add_argument("--root", type=Path, default=Path("Objects"), help="directory containing .pkg files")
    parser.add_argument("--host", default="0.0.0.0", help="bind host")
    parser.add_argument("--port", type=int, default=8080, help="bind port")
    parser.add_argument(
        "--ota-count",
        type=int,
        default=1,
        help="number of upload payloads that receive OTA=1; use -1 for every upload, 0 for never",
    )
    parser.add_argument(
        "--ota-path",
        help="optional package path returned in OTA command, for example /monitor_slot_b_full_v12.pkg",
    )
    parser.add_argument(
        "--ota-version",
        type=int,
        help="optional expected firmware version returned in OTA command",
    )
    parser.add_argument(
        "--idle-timeout",
        type=float,
        default=DEFAULT_IDLE_TIMEOUT_S,
        help="seconds before an idle raw upload connection is closed",
    )
    parser.add_argument(
        "--no-upload-payload",
        action="store_true",
        help="do not print raw UploadTask payloads and parsed OTA report fields",
    )
    parser.add_argument("--self-test", action="store_true", help="run a local protocol self-test and exit")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return

    server = OtaTriggerServer(
        (args.host, args.port),
        OtaTriggerHandler,
        root=args.root,
        ota_count=args.ota_count,
        ota_path=args.ota_path,
        ota_version=args.ota_version,
        idle_timeout_s=args.idle_timeout,
        print_upload_payload=not args.no_upload_payload,
    )
    host, port = server.server_address
    print(f"Serving {args.root.resolve()} on {host}:{port}")
    print("Raw UploadTask payloads receive an OTA command until --ota-count is exhausted.")
    print(f"OTA command: {server.build_ota_command().decode().strip()}")
    print(f"Upload payload logging: {'ON' if server.print_upload_payload else 'OFF'}")
    print("HTTP GET package paths are served from --root.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nserver stopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
