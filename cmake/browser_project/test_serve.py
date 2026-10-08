"""Regression checks for switching deliveries at the same development URL."""
import functools
import http.server
import pathlib
import tempfile
import threading
import unittest
import urllib.request

from serve import Handler


class DeliveryServerTests(unittest.TestCase):
    def test_cached_newer_delivery_does_not_hide_selected_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            content = b'<canvas id="canvas"></canvas>'
            (root / "index.html").write_bytes(content)
            server = http.server.ThreadingHTTPServer(
                ("127.0.0.1", 0), functools.partial(Handler, directory=temporary)
            )
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                url = f"http://127.0.0.1:{server.server_port}/"
                # Another build at this URL may have a newer source timestamp.
                request = urllib.request.Request(
                    url, headers={"If-Modified-Since": "Fri, 01 Jan 2100 00:00:00 GMT"}
                )
                with urllib.request.urlopen(request) as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read(), content)
                    self.assertEqual(response.headers["Cache-Control"], "no-store")
                    self.assertEqual(response.headers["Cross-Origin-Opener-Policy"], "same-origin")
                    self.assertEqual(response.headers["Cross-Origin-Embedder-Policy"], "require-corp")
            finally:
                server.shutdown()
                worker.join()
                server.server_close()


if __name__ == "__main__":
    unittest.main()
