#!/usr/bin/env python3
"""Verify the exact legal text bundled into native builds and installer packages.

Compare the generated CI pin when present; no files are modified.
"""
import hashlib
import json
from pathlib import Path


def verify():
    root = Path(__file__).resolve().parents[1]
    source = (root / 'modules/osci_licensing/state/osci_LegalDocuments.cpp').read_text(encoding='utf-8')
    bundle = json.loads(source.split('R"OSCI_LEGAL(', 1)[1].split(')OSCI_LEGAL"', 1)[0])
    package = (root / 'packaging/legal.txt').read_text(encoding='utf-8-sig')
    assert bundle['scope'] == 'osci-products'
    for document in bundle['documents'].values():
        assert hashlib.sha256(document['text'].encode()).hexdigest() == document['sha256'], 'Invalid document hash'
        assert document['text'] in package, 'Native package text differs from app text'
    manifest = root / 'ci/legal-build.json'
    if manifest.exists():
        pin = json.loads(manifest.read_text(encoding='utf-8'))
        canonical = json.dumps(bundle, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        assert hashlib.sha256(canonical).hexdigest() == pin['sha256'], 'Build snapshot differs from pinned release documents'
    print(f"Legal assets verified: {bundle['revision']}")


if __name__ == '__main__':
    verify()
