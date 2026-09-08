#!/usr/bin/env python3
"""Serve the bounded browser capability fixture on loopback (default port 8000)."""

import argparse
import io
import math
import pathlib
import wave
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


PAGE_DIR = pathlib.Path(__file__).resolve().parent.parent / "test" / "pages"
INDEX_HTML = (PAGE_DIR / "index.html").read_bytes()
with (PAGE_DIR / "fixture.js").open("rb") as fixture_file:
    FIXTURE_JS = fixture_file.read()
PATTERN_WEBM = (PAGE_DIR / "pattern.webm").read_bytes()

DOWNLOAD_TEXT = b"Browser capability fixture download.\n"


def tone_wav():
    """Return a one-second, 8 kHz mono WAV tone generated in memory."""
    output = io.BytesIO()
    with wave.open(output, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(8000)
        samples = bytearray()
        for index in range(8000):
            sample = int(12000 * math.sin(2 * math.pi * 440 * index / 8000))
            samples.extend(sample.to_bytes(2, "little", signed=True))
        wav_file.writeframes(bytes(samples))
    return output.getvalue()


TONE_WAV = tone_wav()


class FixtureHandler(BaseHTTPRequestHandler):
    """Dispatch only the fixed fixture routes and suppress request logging."""

    def log_message(self, _format, *_args):
        return

    def do_GET(self):  # noqa: N802 (BaseHTTPRequestHandler API)
        routes = {
            "/": (INDEX_HTML, "text/html; charset=utf-8", None),
            "/index.html": (INDEX_HTML, "text/html; charset=utf-8", None),
            "/fixture.js": (FIXTURE_JS, "text/javascript; charset=utf-8", None),
            "/download.txt": (DOWNLOAD_TEXT, "text/plain; charset=utf-8", 'attachment; filename="browser-capability-fixture.txt"'),
            "/tone.wav": (TONE_WAV, "audio/wav", None),
            "/pattern.webm": (PATTERN_WEBM, "video/webm", None),
        }
        response = routes.get(self.path)
        if response is None:
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        body, content_type, disposition = response
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        if disposition:
            self.send_header("Content-Disposition", disposition)
        self.end_headers()
        self.wfile.write(body)


def create_server(port=8000):
    """Create a loopback-only fixture server without starting its serving thread."""
    return ThreadingHTTPServer(("127.0.0.1", port), FixtureHandler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000, help="loopback TCP port (default: 8000)")
    args = parser.parse_args()
    server = create_server(args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
