#!/usr/bin/env python3
"""Generate bounded, repeatable performance workloads from a current saved project."""
from __future__ import annotations

import argparse
import copy
import hashlib
from functools import lru_cache
import io
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave
import zipfile
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[2]
ALPHABET = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'


def encode_file(data: bytes) -> str:
    # JUCE MemoryBlock encoding: little-endian six-bit groups, prefixed by byte count.
    chars = []
    for bit in range(0, len(data) * 8, 6):
        offset, shift = divmod(bit, 8)
        word = data[offset] | ((data[offset + 1] if offset + 1 < len(data) else 0) << 8)
        chars.append(ALPHABET[(word >> shift) & 63])
    return f'{len(data)}.' + ''.join(chars)


def decode_file(text: str) -> bytes:
    size, encoded = text.split('.', 1)
    data = bytearray(int(size))
    for index, char in enumerate(encoded):
        offset, shift = divmod(index * 6, 8)
        value = ALPHABET.index(char) << shift
        if offset < len(data):
            data[offset] |= value & 255
        if offset + 1 < len(data):
            data[offset + 1] |= value >> 8
    return bytes(data)


def read_template(path: Path) -> ET.Element:
    data = path.read_bytes()
    if data[:4] == struct.pack('<I', 0x21324356):
        length = struct.unpack_from('<I', data, 4)[0]
        data = data[8:8 + length]
    root = ET.fromstring(data.rstrip(b'\0'))
    version = tuple(int(part) for part in root.get('version', '0').split('.'))
    if root.tag != 'project' or version < (2, 2, 0):
        raise ValueError('Template must be a current app-exported project (version >= 2.2.0)')
    for group in ('effects', 'booleanParameters', 'intParameters', 'files'):
        if root.find(group) is None:
            raise ValueError(f'Template is missing {group}')
    return root


@lru_cache(maxsize=2)
def sources(ffmpeg: str | None = None) -> list[tuple[str, str, bytes]]:
    result = [('defaults', '', b'')]
    examples = [('3d', 'models/cube.obj'), ('3d-heavy', 'models/teapot.obj'),
                ('svg', 'svg/oscilloscope.svg'), ('image', 'oscilloscope/real.png'),
                ('gpla', 'gpla/fallback.gpla'), ('lottie', 'lottie/spinning_squares.lottie'),
                ('fractal', 'fractal/koch_snowflake.lsystem')]
    for kind, relative in examples:
        path = REPO / 'Resources' / relative
        result.append((kind, path.name, path.read_bytes()))
    result.append(('text', 'fixture.txt', b'OSCI deterministic performance workload'))
    for loops in (1, 64):
        code = f'''phase = (phase or 0) + 2 * math.pi * frequency / sample_rate
local x, y = 0, 0
for i = 1, {loops} do
    x = x + math.sin(phase * i) / {loops}
    y = y + math.cos(phase * i) / {loops}
end
return {{x * 0.25, y * 0.25}}
'''
        result.append(('lua' if loops == 1 else 'lua-heavy', f'loops-{loops}.lua', code.encode()))
    output = io.BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        wav.writeframes(b''.join(struct.pack('<hh', int(8192 * math.sin(i * math.tau * 220 / 48000)),
                                            int(8192 * math.cos(i * math.tau * 220 / 48000))) for i in range(48000)))
    result.append(('audio', 'sine.wav', output.getvalue()))
    # Append formats so the original sources retain their stable indexes.
    jpeg = (REPO / 'Resources/oscilloscope/empty.jpg').read_bytes()
    result.extend([('jpeg', 'fixture.jpeg', jpeg), ('jpg', 'fixture.jpg', jpeg),
                   ('flac', 'sosci.flac', (REPO / 'Resources/audio/sosci.flac').read_bytes())])
    with zipfile.ZipFile(REPO / 'Resources/lottie/spinning_squares.lottie') as archive:
        animation = archive.read('animations/12345.json')
    result.extend([('lottie-json', 'spinning_squares.json', animation),
                   ('lottie-lot', 'spinning_squares.lot', animation)])
    if ffmpeg:
        with tempfile.TemporaryDirectory() as directory:
            for extension in ('gif', 'mp4', 'mov'):
                path = Path(directory) / ('fixture.' + extension)
                codec = ['-c:v', 'gif'] if extension == 'gif' else ['-c:v', 'mpeg4', '-pix_fmt', 'yuv420p']
                subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                                'testsrc=size=64x64:rate=8', '-t', '1', '-threads', '1',
                                '-fflags', '+bitexact', '-flags:v', '+bitexact', '-map_metadata', '-1',
                                *codec, str(path)], check=True, timeout=30, capture_output=True)
                result.append((extension, path.name, path.read_bytes()))
    return result


AUDIO_CODECS = {'aiff': 'pcm_s16be', 'ogg': 'vorbis', 'mp3': 'libmp3lame', 'aac': 'aac', 'm4a': 'aac'}


@lru_cache(maxsize=2)
def audio_sources(ffmpeg: str | None) -> tuple[list[tuple[str, str, bytes]], dict[str, str]]:
    if not ffmpeg:
        return [], {extension: 'FFmpeg not available' for extension in AUDIO_CODECS}
    fixtures, skipped = [], {}
    with tempfile.TemporaryDirectory() as directory:
        wav = Path(directory) / 'sine.wav'
        wav.write_bytes(next(data for kind, _, data in sources() if kind == 'audio'))
        for extension, codec in AUDIO_CODECS.items():
            path = Path(directory) / ('sine.' + extension)
            try:
                subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-i', str(wav),
                                '-threads', '1', '-fflags', '+bitexact', '-flags:a', '+bitexact',
                                '-map_metadata', '-1', '-c:a', codec,
                                *(['-strict', 'experimental'] if extension == 'ogg' else []), str(path)],
                               check=True, timeout=30, capture_output=True)
                fixtures.append((extension, path.name, path.read_bytes()))
            except (OSError, subprocess.SubprocessError) as error:
                detail = error.stderr.decode(errors='replace').strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
                skipped[extension] = f'{codec}: {detail.replace(directory, "<temporary>")}'
    return fixtures, skipped


def gpla_line(endpoint: tuple[float, float, float]) -> bytes:
    # One projected line; identical vertices exercise zero-length drawing safely.
    data = b'GPLA    ' + struct.pack('<qqq', 1, 0, 0)
    data += b'FILE    fCount  ' + struct.pack('<q', 1) + b'fRate   ' + struct.pack('<q', 30) + b'DONE    '
    data += b'FRAME   focalLen' + struct.pack('<d', 1.0) + b'OBJECTS OBJECT  MATRIX  '
    data += struct.pack('<16d', *(1.0 if i % 5 == 0 else 0.0 for i in range(16)))
    data += b'DONE    STROKES STROKE  vertexCt' + struct.pack('<q', 2) + b'VERTICES'
    data += struct.pack('<6d', 0.25, 0.25, -1.0, *endpoint)
    return data + b'DONE    ' * 5 + b'END GPLA'


def set_parameter(root: ET.Element, group: str, key: str, value: int) -> None:
    node = root.find(f"{group}/parameter[@id='{key}']")
    if node is None:
        raise ValueError(f'Template is missing parameter {key}')
    node.set('value', str(value))


def generate(template: ET.Element, output: Path, seed: int, count: int, smoke: bool = False, ffmpeg: str | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    source_list = list(sources(ffmpeg))
    legacy_source_count = len(source_list)
    dimensions = [(i, 0, 0, 1, 'embedded', 'off') for i in range(len(source_list))]
    # First cover individual dimensions; seeded combined cases fill the remainder.
    dimensions += [(9, effects, mods, notes, view, mode) for effects, mods, notes, view, mode in [
        (4, 0, 1, 'embedded', 'sustain'), (999, 0, 1, 'embedded', 'sustain'),
        (4, 4, 1, 'embedded', 'sustain'), (4, 16, 1, 'embedded', 'sustain'),
        (0, 0, 4, 'embedded', 'sustain'), (0, 0, 16, 'embedded', 'sustain'),
        (0, 0, 64, 'embedded', 'churn'), (0, 0, 1, 'popout', 'sustain'),
        (0, 0, 1, 'paused', 'sustain'), (0, 0, 1, 'transparent_fullscreen', 'sustain')]]
    def random_dimension():
        return (rng.randrange(len(source_list)), rng.choice([0, 4, 999]), rng.choice([0, 4, 16]),
                rng.choice([1, 4, 16, 64]), rng.choice(['embedded', 'popout', 'paused']), rng.choice(['sustain', 'churn']))

    # Preserve cases 0-29 even when optional FFmpeg fixtures are unavailable.
    while len(dimensions) < 30:
        dimensions.append(random_dimension())
    mixed_cases = {
        len(dimensions): [1, 3, 8, 9],
        len(dimensions) + 1: list(range(1, len(source_list))),
    }
    dimensions.extend([(9, 0, 0, 1, 'embedded', 'off')] * len(mixed_cases))
    codec_sources, codec_skips = audio_sources(ffmpeg)
    source_list.extend(codec_sources)
    dimensions.extend((i, 0, 0, 1, 'embedded', 'off') for i in range(legacy_source_count, len(source_list)))
    modulation_cases = {len(dimensions) + i: kind for i, kind in enumerate(('env', 'rng', 'sc'))}
    dimensions.extend([(3, 4, 4, 4, 'embedded', 'churn')] * len(modulation_cases))
    for name, endpoint in [('zero-length.gpla', (0.25, 0.25, -1.0)), ('single-line.gpla', (0.5, 0.5, -1.0))]:
        dimensions.append((len(source_list), 0, 0, 1, 'embedded', 'off'))
        source_list.append(('gpla', name, gpla_line(endpoint)))
    encoded_sources = {}
    while len(dimensions) < count:
        dimensions.append(random_dimension())
    manifest = {'schemaVersion': 1, 'seed': seed, 'generator_dependencies': ['ffmpeg'] if ffmpeg else [],
                'skipped_formats': ([] if ffmpeg else ['gif', 'mp4', 'mov']) + list(codec_skips),
                'skip_reasons': codec_skips, 'cases': []}
    for index, (source_index, effect_count, mods, notes, view, mode) in enumerate(dimensions[:count]):
        root = copy.deepcopy(template)
        kind, filename, payload = source_list[source_index]
        case_sources = mixed_cases.get(index, [source_index])
        if filename:
            files = root.find('files')
            files.clear()
            for file_index in case_sources:
                _, file_name, file_payload = source_list[file_index]
                if file_index not in encoded_sources:
                    encoded_sources[file_index] = encode_file(file_payload)
                ET.SubElement(files, 'file', name=file_name).text = encoded_sources[file_index]
            root.set('currentFile', str(case_sources.index(source_index)))
        case_kinds = [source_list[file_index][0] for file_index in case_sources]
        modulation_type = modulation_cases.get(index, 'lfo')
        toggleable = [effect for effect in root.findall('effects/effect') if effect.find('selected') is not None]
        if index == 0:
            selected = [effect for effect in toggleable if effect.find('selected').get('value') == '1']
            targets = []
            mods = 0
            voices = int(root.find("intParameters/parameter[@id='voices']").get('value', '4'))
            mode = 'sustain' if root.find("booleanParameters/parameter[@id='midiEnabled']").get('value') == '1' else 'off'
            notes = voices
        else:
            selected = toggleable[:effect_count]
            for effect in toggleable:
                for flag in ('selected', 'enabled'):
                    node = effect.find(flag)
                    if node is not None:
                        node.set('value', str(int(effect in selected)))
                        # These flags are also serialized globally, and that copy loads last.
                        duplicate = root.find(f"booleanParameters/parameter[@id='{node.get('id')}']")
                        if duplicate is not None:
                            duplicate.set('value', node.get('value'))
            # Keep modulation isolated and avoid dangling assignments inherited from the seed.
            for path in ('lfoAssignments', 'envelopes/envAssignments', 'randoms/randomAssignments', 'sidechain/sidechainAssignments'):
                node = root.find(path)
                if node is not None:
                    node.clear()
            targets = [p.get('id') for e in selected for p in e.findall('parameter') if p.get('id')]
            assignment_path = {'lfo': 'lfoAssignments', 'env': 'envelopes/envAssignments',
                               'rng': 'randoms/randomAssignments', 'sc': 'sidechain/sidechainAssignments'}[modulation_type]
            assignments = root
            for tag in assignment_path.split('/'):
                child = assignments.find(tag)
                if child is None:
                    child = ET.SubElement(assignments, tag)
                assignments = child
            for target in targets[:mods]:
                ET.SubElement(assignments, 'assignment', {modulation_type: '0', 'param': target, 'depth': '0.25', 'bipolar': '1'})
            voices = min(notes, 16)
            set_parameter(root, 'intParameters', 'voices', voices)
            set_parameter(root, 'booleanParameters', 'midiEnabled', int(mode != 'off'))
        data = ET.tostring(root, encoding='utf-8', xml_declaration=True)
        case_id = f'{index:03d}-{kind}-{hashlib.sha256(data + view.encode() + mode.encode() + str(notes).encode()).hexdigest()[:10]}'
        project = case_id + '.osci'
        (output / project).write_bytes(data)
        manifest['cases'].append({'id': case_id, 'project': project, 'source': {'kind': kind, 'name': filename},
            'expect_nonzero_audio': filename != 'zero-length.gpla',
            'file_count': len(root.findall('files/file')), 'selected_file_index': int(root.get('currentFile', '-1')),
            'effects': len(selected), 'modulation': min(mods, len(targets)), 'modulation_type': modulation_type if mods else 'none', 'voices': voices,
            'host_input_amplitude': 0.25 if modulation_type == 'sc' else 0.0,
            'midi': {'mode': mode, 'enabled': mode != 'off', 'requested_notes': notes, 'notes': list(range(60, 60 + notes)), 'velocity': 80},
            'presentation': {'mode': view}, 'requires_premium': any(item.startswith('lottie') or item in ('fractal', 'mp4', 'mov') for item in case_kinds) or mods > 0,
            'runtime_dependencies': ['ffmpeg'] if any(item in ('mp4', 'mov') for item in case_kinds) else [],
            'fixture_dependencies': ['ffmpeg'] if any(item in ('gif', 'mp4', 'mov') or item in AUDIO_CODECS for item in case_kinds) else [],
            'supported_platforms': ['darwin'] if any(item in ('aac', 'm4a') for item in case_kinds) else ['darwin', 'linux', 'win32'],
            'sweep': {'sample_rates': [48000] if smoke else [44100, 48000, 96000],
                      'block_sizes': [256] if smoke else [64, 256, 1024], 'ratios': [1] if smoke else [1, 2, 4]},
            'benchmark_args': ['--voices', str(voices), '--midi', mode, '--warmup', '100' if smoke else '2000', '--blocks', '300' if smoke else '5000']})
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


class GeneratorTests(unittest.TestCase):
    def test_mixed_sources(self):
        root = ET.fromstring('<project version="3.0.0"><effects/><files/><booleanParameters><parameter id="midiEnabled" value="0"/></booleanParameters><intParameters><parameter id="voices" value="4"/></intParameters></project>')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            cases = generate(root, output, 3, 32, True)['cases']
            mixed = [case for case in cases if case['file_count'] > 1]
            self.assertEqual([case['file_count'] for case in mixed], [4, len(sources()) - 1])
            for case in mixed:
                files = read_template(output / case['project']).findall('files/file')
                self.assertEqual(files[case['selected_file_index']].get('name'), case['source']['name'])
                self.assertEqual(case['source']['kind'], 'lua')
            self.assertTrue(mixed[-1]['requires_premium'])

    def test_codec_cases_preserve_existing_order(self):
        root = ET.fromstring('<project version="3.0.0"><effects/><files/><booleanParameters><parameter id="midiEnabled" value="0"/></booleanParameters><intParameters><parameter id="voices" value="4"/></intParameters></project>')
        old_sources = sources() + [(kind, f'fixture.{kind}', b'fixture') for kind in ('gif', 'mp4', 'mov')]
        codecs = [(kind, f'sine.{kind}', b'fixture') for kind in AUDIO_CODECS]
        with tempfile.TemporaryDirectory() as directory, patch(__name__ + '.sources', return_value=old_sources):
            a, b = Path(directory) / 'a', Path(directory) / 'b'
            with patch(__name__ + '.audio_sources', return_value=([], {})):
                before = generate(root, a, 376, 40, True, 'ffmpeg')
            with patch(__name__ + '.audio_sources', return_value=(codecs, {})):
                after = generate(root, b, 376, 40, True, 'ffmpeg')
            self.assertEqual(before['cases'][:32], after['cases'][:32])
            for case in before['cases'][:32]:
                self.assertEqual((a / case['project']).read_bytes(), (b / case['project']).read_bytes())
            added = after['cases'][32:37]
            self.assertEqual([case['source']['kind'] for case in added], list(AUDIO_CODECS))
            for case in added:
                self.assertEqual(case['fixture_dependencies'], ['ffmpeg'])
                self.assertEqual(case['runtime_dependencies'], [])
                self.assertEqual(case['supported_platforms'] == ['darwin'], case['source']['kind'] in ('aac', 'm4a'))

    def test_modulation_sources_and_zero_geometry(self):
        root = ET.fromstring('<project version="3.0.0"><effects><effect><selected value="0"/><parameter id="depth"/></effect></effects><files/><booleanParameters><parameter id="midiEnabled" value="0"/></booleanParameters><intParameters><parameter id="voices" value="4"/></intParameters></project>')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            cases = generate(root, output, 3, 37, True)['cases']
            for index, kind, path in [(32, 'env', 'envelopes/envAssignments'),
                                      (33, 'rng', 'randoms/randomAssignments'),
                                      (34, 'sc', 'sidechain/sidechainAssignments')]:
                case = cases[index]
                project = read_template(output / case['project'])
                assignment = project.find(path + '/assignment')
                self.assertEqual(assignment.get(kind), '0')
                self.assertEqual(assignment.get('param'), 'depth')
                self.assertEqual(case['modulation_type'], kind)
                self.assertNotEqual(case['source']['kind'], 'lua')
                self.assertEqual(case['host_input_amplitude'], 0.25 if kind == 'sc' else 0.0)
            self.assertEqual(cases[35]['source']['name'], 'zero-length.gpla')
            self.assertEqual(cases[36]['source']['name'], 'single-line.gpla')
            payload = decode_file(read_template(output / cases[35]['project']).find('files/file').text)
            self.assertEqual(len(payload), 400)
            start = payload.index(b'VERTICES') + 8
            vertices = struct.unpack_from('<6d', payload, start)
            self.assertEqual(vertices[:3], vertices[3:])
            self.assertLess(vertices[2], 0)

    def test_unavailable_audio_encoders(self):
        with patch(__name__ + '.subprocess.run', side_effect=FileNotFoundError('missing encoder')):
            fixtures, skipped = audio_sources.__wrapped__('missing-ffmpeg')
        self.assertEqual(fixtures, [])
        self.assertEqual(set(skipped), set(AUDIO_CODECS))
        self.assertTrue(all('missing encoder' in reason for reason in skipped.values()))
        self.assertEqual(set(audio_sources(None)[1]), set(AUDIO_CODECS))

    def test_duplicate_effect_flags(self):
        root = ET.fromstring('<project version="3.0.0"><effects><effect id="test"><selected id="testSelected" value="0"/><enabled id="testEnabled" value="0"/></effect></effects><files/><booleanParameters><parameter id="midiEnabled" value="0"/><parameter id="testSelected" value="0"/><parameter id="testEnabled" value="0"/></booleanParameters><intParameters><parameter id="voices" value="4"/></intParameters></project>')
        with tempfile.TemporaryDirectory() as directory:
            manifest = generate(root, Path(directory), 4, 32, True)
            for case in manifest['cases']:
                project = read_template(Path(directory) / case['project'])
                for flag in ('selected', 'enabled'):
                    local = project.find(f'effects/effect/{flag}')
                    global_flag = project.find(f"booleanParameters/parameter[@id='{local.get('id')}']")
                    self.assertEqual(local.get('value'), global_flag.get('value'))

    def test_encoding(self):
        rng = random.Random(3)
        for size in (0, 1, 2, 3, 7, 256):
            data = bytes(rng.randrange(256) for _ in range(size))
            self.assertEqual(decode_file(encode_file(data)), data)
        self.assertEqual(encode_file(b'\0'), '1...')

    def test_appended_formats(self):
        fixtures = sources()
        self.assertEqual(fixtures[9][0], 'lua')
        self.assertEqual(fixtures[11][0], 'audio')
        self.assertEqual({name.rsplit('.', 1)[-1] for _, name, _ in fixtures[12:]}, {'jpg', 'jpeg', 'flac', 'json', 'lot'})
        for kind, _, data in fixtures:
            if kind.startswith('lottie-'):
                self.assertIn('layers', json.loads(data))

    def test_determinism_and_schema(self):
        root = ET.fromstring('<project version="3.0.0"><effects/><files/><booleanParameters><parameter id="midiEnabled" value="0"/></booleanParameters><intParameters><parameter id="voices" value="4"/></intParameters></project>')
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / 'a', Path(directory) / 'b'
            self.assertEqual(generate(root, a, 4, 25, True), generate(root, b, 4, 25, True))
            for path in a.glob('*.osci'):
                self.assertEqual(path.read_bytes(), (b / path.name).read_bytes())
                project = read_template(path)
                for file in project.findall('files/file'):
                    self.assertTrue(decode_file(file.text))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--template', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--count', type=int, default=32)
    parser.add_argument('--smoke', action='store_true', help='Short benchmark durations and one host configuration')
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg'), help='FFmpeg executable for tiny video and audio codec fixtures; unavailable encoders are reported skipped')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        result = unittest.main(argv=['generate_projects'], exit=False).result
        sys.exit(not result.wasSuccessful())
    if args.template is None or args.output is None or not 1 <= args.count <= 1000:
        parser.error('--template, --output and --count in [1, 1000] are required')
    generate(read_template(args.template), args.output, args.seed, args.count, args.smoke, args.ffmpeg)


if __name__ == '__main__':
    main()
