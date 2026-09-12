import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from prepare_legal import generate, validate_snapshot


class PrepareLegalTests(unittest.TestCase):
    def setUp(self):
        self.bundle = dict(scope='osci-products', revision='test-1', documents={})
        for kind in ('privacy', 'terms'):
            text = ('UTF-8 — document ' + kind + '\n') * 20
            self.bundle['documents'][kind] = dict(revision='test-1', text=text, summary='', sha256=hashlib.sha256(text.encode()).hexdigest())
        self.raw = json.dumps(self.bundle, ensure_ascii=False).encode()
        self.pin = dict(revision='test-1', sha256=hashlib.sha256(self.raw).hexdigest())

    def test_exact_snapshot_generates_matching_offline_files(self):
        bundle = validate_snapshot(self.raw, self.pin)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'modules/osci_licensing/state').mkdir(parents=True)
            (root / 'packaging').mkdir()
            generate(bundle, root)
            source = (root / 'modules/osci_licensing/state/osci_LegalDocuments.cpp').read_text(encoding='utf-8')
            embedded = json.loads(source.split('R"OSCI_LEGAL(')[1].split(')OSCI_LEGAL"')[0])
            self.assertEqual(bundle, embedded)
            self.assertIn(bundle['documents']['privacy']['text'], (root / 'packaging/legal.txt').read_text(encoding='utf-8'))

    def test_mismatch_and_invalid_documents_fail_closed(self):
        with self.assertRaises(ValueError):
            validate_snapshot(self.raw + b' ', self.pin)
        self.bundle['documents']['privacy']['text'] += 'changed'
        raw = json.dumps(self.bundle).encode()
        with self.assertRaises(ValueError):
            validate_snapshot(raw, dict(revision='test-1', sha256=hashlib.sha256(raw).hexdigest()))


if __name__ == '__main__':
    unittest.main()
