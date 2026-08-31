import io
from pathlib import Path
import tempfile
import unittest
import urllib.error
from unittest.mock import MagicMock, patch

from publish_release import http_put_file


class UploadTests(unittest.TestCase):
    def test_retries_transient_failures_from_the_start_of_the_file(self):
        for failure in (500, 502, 503, 504, 408, 429, TimeoutError(), urllib.error.URLError('reset')):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                artifact = Path(directory) / 'artifact.zip'
                artifact.write_bytes(b'complete artifact')
                uploaded = []

                def upload(request, timeout):
                    uploaded.append(request.data.read())
                    self.assertEqual(request.get_header('Content-length'), str(artifact.stat().st_size))
                    if len(uploaded) == 1:
                        if isinstance(failure, int):
                            raise urllib.error.HTTPError(request.full_url, failure, 'temporary', {}, io.BytesIO(b'retry'))
                        raise failure
                    response = MagicMock()
                    response.__enter__.return_value.status = 200
                    return response

                with patch('publish_release.urllib.request.urlopen', side_effect=upload), patch('publish_release.time.sleep') as sleep:
                    self.assertEqual(http_put_file('https://upload.invalid/file', artifact), 200)
                    self.assertEqual(uploaded, [b'complete artifact', b'complete artifact'])
                    sleep.assert_called_once_with(2)

    def test_permanent_failures_are_not_retried_and_transient_retries_are_bounded(self):
        for status, attempts in ((403, 1), (500, 3)):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                artifact = Path(directory) / 'artifact.zip'
                artifact.write_bytes(b'artifact')

                def upload(request, timeout):
                    raise urllib.error.HTTPError(request.full_url, status, 'failed', {}, io.BytesIO(b'error'))

                with patch('publish_release.urllib.request.urlopen', side_effect=upload) as request, patch('publish_release.time.sleep') as sleep:
                    with self.assertRaises(SystemExit):
                        http_put_file('https://upload.invalid/file', artifact)
                    self.assertEqual(request.call_count, attempts)
                    self.assertEqual(sleep.call_count, attempts - 1)


if __name__ == '__main__':
    unittest.main()
