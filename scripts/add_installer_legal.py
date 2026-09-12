#!/usr/bin/env python3
"""Add the bundled legal documents to an unsigned macOS distribution package.

Run before productsign/notarization. Only build output is rewritten.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def add_legal(package: Path, legal_text: Path):
    if package.suffix != '.pkg' or not package.is_file():
        raise ValueError('Expected an existing unsigned .pkg build artifact')
    with tempfile.TemporaryDirectory(prefix='osci-legal-package-') as directory:
        expanded = Path(directory) / 'expanded'
        subprocess.run(['pkgutil', '--expand', str(package), str(expanded)], check=True)
        distribution = expanded / 'Distribution'
        if not distribution.is_file():
            raise ValueError('Expected a distribution package')
        tree = ET.parse(distribution)
        root = tree.getroot()
        for old in root.findall('license'):
            root.remove(old)
        ET.SubElement(root, 'license', {'file': 'osci-legal.txt', 'mime-type': 'text/plain'})
        resources = expanded / 'Resources'
        resources.mkdir(exist_ok=True)
        shutil.copyfile(legal_text, resources / 'osci-legal.txt')
        tree.write(distribution, encoding='utf-8', xml_declaration=True)
        output = Path(directory) / 'legal.pkg'
        subprocess.run(['pkgutil', '--flatten', str(expanded), str(output)], check=True)
        shutil.copyfile(output, package)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    add_legal(args.package.resolve(), Path(__file__).resolve().parents[1] / 'packaging/legal.txt')
