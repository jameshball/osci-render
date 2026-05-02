#!/usr/bin/env python3
"""Publish a single release artifact to api.osci-render.com.

Used by the CI release pipeline (and runnable manually) to:

1. Compute SHA-256 of an artifact on disk.
2. Sign ``"<semver>|<platform>|<sha256_hex>"`` with the Ed25519
   release-signing key (32-byte seed, base64-encoded).
3. POST to ``/api/admin/version/publish`` with bearer auth.
4. Upload the artifact to the returned presigned R2 URL.

Environment variables (all required):

    PUBLISH_API_BASE              e.g. https://api.osci-render.com
    PUBLISH_API_TOKEN             bearer token (server PUBLISH_API_TOKEN secret)
    RELEASE_SIGNING_PRIVATE_KEY   base64-encoded 32-byte Ed25519 seed
                                  (or pass --release-key path/to/seed.b64)

Example::

    python3 ci/publish_release.py \\
        --product Hsr9C58_YhTxYP0MNvsIow== \\
        --semver 2.6.0.0 \\
        --platform mac-universal \\
        --artifact-kind pkg \\
        --artifact bin/sosci-mac.pkg \\
        --notes "Bug fixes."
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Optional


def b64decode_seed(value: str) -> bytes:
    """Tolerantly decode a base64-encoded 32-byte Ed25519 seed."""
    s = value.strip()
    s += '=' * (-len(s) % 4)
    seed = base64.b64decode(s)
    if len(seed) != 32:
        raise SystemExit(f'Expected 32-byte Ed25519 seed, got {len(seed)} bytes')
    return seed


def load_release_key(*, env_value: Optional[str], path: Optional[str]) -> bytes:
    if path:
        return b64decode_seed(Path(path).read_text())
    if env_value:
        return b64decode_seed(env_value)
    raise SystemExit('release signing key not provided (set RELEASE_SIGNING_PRIVATE_KEY or --release-key)')


def sign_release(seed: bytes, *, semver: str, platform: str, sha256_hex: str) -> str:
    """Sign ``<semver>|<platform>|<sha256_hex>`` with Ed25519. Returns base64."""
    try:
        from nacl.signing import SigningKey
    except ImportError:
        raise SystemExit('PyNaCl is required: pip install pynacl')
    msg = f'{semver}|{platform}|{sha256_hex.lower()}'.encode('utf-8')
    sig = SigningKey(seed).sign(msg).signature
    return base64.b64encode(sig).decode('ascii')


def sha256_file(path: Path, *, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        while True:
            buf = f.read(chunk)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest()


def http_post_json(url: str, body: dict, *, headers: dict, timeout: float = 60.0) -> dict:
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode('utf-8'),
        headers={'Content-Type': 'application/json', **headers},
        method='POST',
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        raise SystemExit(f'POST {url} -> HTTP {e.code}: {e.read().decode("utf-8", errors="replace")}')


def http_put_file(url: str, path: Path, *, content_type: str = 'application/octet-stream', timeout: float = 600.0) -> int:
    """Stream-upload a file to a presigned URL."""
    size = path.stat().st_size
    with path.open('rb') as f:
        req = urllib.request.Request(
            url,
            data=f,
            headers={'Content-Type': content_type, 'Content-Length': str(size)},
            method='PUT',
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.status
        except urllib.error.HTTPError as e:
            raise SystemExit(f'PUT presigned URL failed: HTTP {e.code}: {e.read().decode("utf-8", errors="replace")}')


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog='publish_release.py', description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--product', required=True,
                   help='Gumroad product id, Payhip product link, or numeric internal id')
    p.add_argument('--semver', required=True, help='e.g. 2.6.0.0')
    p.add_argument('--platform', required=True,
                   choices=['mac-arm64', 'mac-x86_64', 'mac-universal', 'win-x86_64', 'linux-x86_64'])
    p.add_argument('--release-track', default='alpha', choices=['alpha', 'beta', 'stable'],
                   help='Release track to register with the API. CI should publish alpha.')
    p.add_argument('--variant', default='premium', choices=['free', 'premium'],
                   help='Licensing variant of the build (free or premium); separate Version row per variant.')
    p.add_argument('--artifact-kind', required=True, choices=['pkg', 'exe', 'zip', 'appimage', 'tar.gz'])
    p.add_argument('--artifact', required=True, type=Path,
                   help='Path to the artifact file on disk; uploaded as-is to R2')
    p.add_argument('--filename', default=None,
                   help='Object key filename (defaults to the artifact basename)')
    p.add_argument('--notes', default=None, help='Inline release notes (markdown)')
    p.add_argument('--notes-file', default=None, type=Path, help='Read release notes from file')
    p.add_argument('--min-supported-from', default=None,
                   help='Optional: oldest semver that should auto-upgrade to this build')
    p.add_argument('--api-base', default=os.environ.get('PUBLISH_API_BASE', 'https://api.osci-render.com'))
    p.add_argument('--api-token', default=os.environ.get('PUBLISH_API_TOKEN'),
                   help='Bearer token; defaults to $PUBLISH_API_TOKEN')
    p.add_argument('--release-key', default=None,
                   help='Path to file containing base64-encoded 32-byte Ed25519 seed; '
                        'alternatively set $RELEASE_SIGNING_PRIVATE_KEY')
    p.add_argument('--dry-run', action='store_true',
                   help='Compute sha + sig and print the request body, but do not POST')
    args = p.parse_args(argv)

    if not args.api_token:
        raise SystemExit('--api-token or $PUBLISH_API_TOKEN is required')
    if not args.artifact.exists():
        raise SystemExit(f'artifact does not exist: {args.artifact}')

    seed = load_release_key(
        env_value=os.environ.get('RELEASE_SIGNING_PRIVATE_KEY'),
        path=args.release_key,
    )

    notes = args.notes
    if args.notes_file:
        notes = args.notes_file.read_text(encoding='utf-8')

    print(f'Hashing {args.artifact} ({args.artifact.stat().st_size:,} bytes)...', flush=True)
    sha256 = sha256_file(args.artifact)
    print(f'  sha256 = {sha256}')

    sig = sign_release(seed, semver=args.semver, platform=args.platform, sha256_hex=sha256)
    print(f'  ed25519_sig = {sig[:32]}...')

    filename = args.filename or args.artifact.name
    body = {
        'product': args.product,
        'semver': args.semver,
        'platform': args.platform,
        'release_track': args.release_track,
        'variant': args.variant,
        'artifact_kind': args.artifact_kind,
        'filename': filename,
        'sha256': sha256,
        'ed25519_sig': sig,
    }
    if notes:
        body['notes_md'] = notes
    if args.min_supported_from:
        body['min_supported_from'] = args.min_supported_from

    if args.dry_run:
        print('DRY RUN -- would POST:')
        print(json.dumps(body, indent=2))
        return 0

    print(f'POST {args.api_base}/api/admin/version/publish ...', flush=True)
    resp = http_post_json(
        f'{args.api_base}/api/admin/version/publish',
        body,
        headers={'Authorization': f'Bearer {args.api_token}'},
    )
    print(f'  registered version_id={resp["version_id"]} object_key={resp["object_key"]}')

    print(f'PUT artifact -> R2 (expires {resp["expires_at"]})...', flush=True)
    status = http_put_file(resp['upload_url'], args.artifact)
    print(f'  upload status: {status}')

    if status not in (200, 204):
        raise SystemExit(f'unexpected upload status: {status}')

    print(f'\nDONE: {args.product} {args.semver} {args.platform} -> {resp["object_key"]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
