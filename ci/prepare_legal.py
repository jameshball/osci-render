#!/usr/bin/env python3
"""Pin release documents centrally and generate the offline build inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.request
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
REVISION = re.compile(r'^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$')


def request_bytes(url, body=None, token=None):
    headers = {'Accept': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    if body is not None:
        headers['Content-Type'] = 'application/json'
    request = urllib.request.Request(url, data=json.dumps(body).encode() if body is not None else None, headers=headers)
    # An authenticated request must never forward credentials through a redirect.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=30) as response:
        result = response.read(1000001)
        if len(result) > 1000000:
            raise ValueError('Document response is too large')
        return result


def validate_snapshot(raw, manifest):
    if hashlib.sha256(raw).hexdigest() != manifest['sha256']:
        raise ValueError('Bundle hash mismatch')
    bundle = json.loads(raw)
    if bundle['scope'] != 'osci-products' or bundle['revision'] != manifest['revision'] or not REVISION.fullmatch(bundle['revision']):
        raise ValueError('Bundle identity mismatch')
    for kind in ('privacy', 'terms'):
        doc = bundle['documents'][kind]
        if not REVISION.fullmatch(doc['revision']) or not isinstance(doc['text'], str) or not 100 <= len(doc['text']) <= 100000:
            raise ValueError('Invalid document')
        if hashlib.sha256(doc['text'].encode('utf-8')).hexdigest() != doc['sha256']:
            raise ValueError('Document hash mismatch')
    return bundle


def generate(bundle, root=ROOT):
    snapshot = json.dumps(bundle, ensure_ascii=False, separators=(',', ':'))
    source = '// Generated from a pinned Release-plane snapshot. Do not edit document text here.\n'
    source += 'namespace osci {\njuce::var LegalState::bundledDocuments() {\n'
    source += '    return juce::JSON::parse(juce::String::fromUTF8(R"OSCI_LEGAL(' + snapshot + ')OSCI_LEGAL"));\n}\n}\n'
    (root / 'modules/osci_licensing/state/osci_LegalDocuments.cpp').write_text(source, encoding='utf-8')
    intro = 'Review the terms and privacy policy before continuing.\n\n'
    (root / 'packaging/legal.txt').write_text(intro + bundle['documents']['terms']['text'] + '\n\n' + bundle['documents']['privacy']['text'] + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--product', required=True)
    parser.add_argument('--semver', required=True)
    parser.add_argument('--api-base', default=os.environ.get('PUBLISH_API_BASE', 'https://jameshball.releaseplane.org'))
    parser.add_argument('--public-base', default='https://documents.releaseplane.org')
    args = parser.parse_args()
    token = os.environ.get('PUBLISH_API_TOKEN')
    if not token or urlsplit(args.api_base).scheme != 'https':
        raise ValueError('An HTTPS API and publish credential are required')
    target = dict(product=args.product, semver=args.semver, release_track='alpha')
    response = json.loads(request_bytes(args.api_base.rstrip('/') + '/api/admin/version/legal-build', target, token))
    manifest = response['static']
    url = urlsplit(manifest['url'])
    if url.scheme != 'https' or url.netloc != urlsplit(args.public_base).netloc or url.username or url.fragment or url.query:
        raise ValueError('Unexpected static document origin')
    bundle = validate_snapshot(request_bytes(manifest['url']), manifest)
    generate(bundle)
    (ROOT / 'ci/legal-build.json').write_text(json.dumps(dict(**target, **manifest)), encoding='utf-8')
    print('Pinned offline documents: ' + bundle['revision'])


if __name__ == '__main__':
    try:
        main()
    except Exception:
        raise SystemExit('Document preparation failed. Check release assignment, delivery and build credentials; no fallback was used.')
