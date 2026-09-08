import http.client
import importlib.util
import pathlib
import threading
import unittest


SERVER_PATH = pathlib.Path(__file__).parents[1] / "tools" / "serve_test_pages.py"
SPEC = importlib.util.spec_from_file_location("serve_test_pages", SERVER_PATH)
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)


class FixtureServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = SERVER.create_server(0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        if cls.thread.is_alive():
            raise AssertionError("fixture server thread did not stop")

    def get(self, path):
        connection = http.client.HTTPConnection("127.0.0.1", self.port)
        connection.request("GET", path)
        response = connection.getresponse()
        body = response.read()
        headers = response.getheaders()
        connection.close()
        return response.status, dict(headers), body

    def test_allowed_routes_and_content_types(self):
        for path in ("/", "/index.html"):
            status, headers, body = self.get(path)
            self.assertEqual(status, 200)
            self.assertIn("text/html", headers["Content-Type"])
            self.assertIn(b"Browser capability fixture", body)
        status, headers, body = self.get("/fixture.js")
        self.assertEqual(status, 200)
        self.assertIn("text/javascript", headers["Content-Type"])
        self.assertIn(b"indexedDB", body)

    def test_unknown_and_traversal_routes_are_not_exposed(self):
        for path in ("/missing", "/../README.md", "/%2e%2e/README.md"):
            status, _headers, _body = self.get(path)
            self.assertEqual(status, 404)

    def test_download_has_fixed_text_and_attachment_header(self):
        status, headers, body = self.get("/download.txt")
        self.assertEqual(status, 200)
        self.assertEqual(headers["Content-Disposition"], 'attachment; filename="browser-capability-fixture.txt"')
        self.assertEqual(body, SERVER.DOWNLOAD_TEXT)

    def test_audio_is_wav(self):
        status, headers, body = self.get("/tone.wav")
        self.assertEqual(status, 200)
        self.assertEqual(headers["Content-Type"], "audio/wav")
        self.assertEqual(body[:4], b"RIFF")
        self.assertEqual(body[8:12], b"WAVE")
        self.assertGreater(len(body), 16000)

    def test_video_is_fixed_webm(self):
        status, headers, body = self.get("/pattern.webm")
        self.assertEqual(status, 200)
        self.assertEqual(headers["Content-Type"], "video/webm")
        self.assertEqual(headers["Content-Length"], str(len(SERVER.PATTERN_WEBM)))
        self.assertEqual(body, SERVER.PATTERN_WEBM)
        self.assertEqual(body[:4], b"\x1a\x45\xdf\xa3")


if __name__ == "__main__":
    unittest.main()
