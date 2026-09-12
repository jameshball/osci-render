#!/usr/bin/env python3
"""Verify the exact legal text bundled into native builds and installer packages.

Optionally compare the sibling website snapshot; no files are modified.
"""
import argparse
import hashlib
import json
from pathlib import Path


def verify(website: Path | None = None):
    root = Path(__file__).resolve().parents[1]
    source = (root / 'modules/osci_licensing/state/osci_LegalDocuments.cpp').read_text(encoding='utf-8')
    bundle = json.loads(source.split('R"OSCI_LEGAL(', 1)[1].split(')OSCI_LEGAL"', 1)[0])
    package = (root / 'packaging/legal.txt').read_text(encoding='utf-8-sig')
    assert bundle['scope'] == 'osci-products'
    for document in bundle['documents'].values():
        assert hashlib.sha256(document['text'].encode()).hexdigest() == document['sha256'], 'Invalid document hash'
        assert document['text'] in package, 'Native package text differs from app text'
    if website:
        snapshot = json.loads((website / f"public/legal/{bundle['revision']}.json").read_text(encoding='utf-8'))
        current = json.loads((website / 'src/legal/documents.json').read_text(encoding='utf-8'))
        assert snapshot == current, 'Website current and archived snapshot differ'
        for kind, document in bundle['documents'].items():
            assert {k: v for k, v in document.items() if k != 'sha256'} == snapshot['documents'][kind], 'Website and native documents differ'
    print(f"Legal assets verified: {bundle['revision']}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--website', type=Path)
    verify(parser.parse_args().website)
