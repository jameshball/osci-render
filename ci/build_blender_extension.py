"""Build the public Blender repository with Blender's official tools."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / "blender/osci_render"
    manifest = tomllib.loads((source / "blender_manifest.toml").read_text())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f"{manifest['id']}-{manifest['version']}.zip"
    if archive.exists():
        raise SystemExit(f"Refusing to overwrite a release archive: {archive}")
    with tempfile.TemporaryDirectory(prefix="osci-blender-build-") as temporary:
        stage = Path(temporary) / "source"
        stage.mkdir()
        for name in ("__init__.py", "blender_manifest.toml"):
            shutil.copyfile(source / name, stage / name)
        shutil.copyfile(root / "LICENSE", stage / "LICENSE")
        subprocess.run([args.blender, "--command", "extension", "validate", str(stage)], check=True)
        subprocess.run([args.blender, "--command", "extension", "build", "--source-dir", str(stage),
                        "--output-filepath", str(archive)], check=True)
        with zipfile.ZipFile(archive) as package:
            assert set(package.namelist()) == {"__init__.py", "blender_manifest.toml", "LICENSE"}
            contents = {name: package.read(name) for name in package.namelist()}
        # Blender's builder uses source mtimes. Normalize the generated archive so
        # retrying publication of the same sources produces exactly the same hash.
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as package:
            for name in sorted(contents):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                package.writestr(info, contents[name], compresslevel=9)
        # Generate the index from this release only; old archives can remain available
        # at immutable URLs without becoming candidates for Blender to install.
        repository = Path(temporary) / "repository"
        repository.mkdir()
        shutil.copyfile(archive, repository / archive.name)
        subprocess.run([args.blender, "--command", "extension", "server-generate", "--repo-dir", str(repository)], check=True)
        listing = json.loads((repository / "index.json").read_text())
        assert len(listing["data"]) == 1
        item = listing["data"][0]
        assert item["archive_hash"] == "sha256:" + hashlib.sha256(archive.read_bytes()).hexdigest()
        shutil.copyfile(repository / "index.json", output / "index.json")
    print(f"Repository ready: {output}")


if __name__ == "__main__":
    main()
