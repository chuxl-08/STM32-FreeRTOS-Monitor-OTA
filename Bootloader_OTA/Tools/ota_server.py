#!/usr/bin/env python3
"""Serve firmware packages over HTTP for the later ESP01 OTA stage."""

from __future__ import annotations

import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Start a local firmware HTTP server.")
    parser.add_argument("--dir", type=Path, default=Path.cwd(), help="directory to serve")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    handler = functools.partial(SimpleHTTPRequestHandler, directory=str(args.dir))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Serving {args.dir.resolve()} at http://{args.host}:{args.port}/")
    server.serve_forever()


if __name__ == "__main__":
    main()

