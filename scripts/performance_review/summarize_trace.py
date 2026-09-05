#!/usr/bin/env python3
"""Summarize exported xctrace CPU Profiler samples (cycle weights, not wall time)."""
import argparse
from collections import Counter
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def summarize(path, scope='', after=0, thread='', limit=40):
    tree = ET.parse(path)
    references = {e.get('id'): e for e in tree.iter() if e.get('id')}
    def resolve(element):
        return references[element.get('ref')] if element.get('ref') else element
    inclusive, leaf, threads = Counter(), Counter(), Counter()
    total, samples, unresolved = 0, 0, 0
    for row in tree.findall('.//row'):
        cells = [resolve(e) for e in row]
        if len(cells) != 7 or cells[6].tag != 'backtrace':
            continue
        if int(cells[0].text or 0) < after * 1e9:
            continue
        if thread and thread not in cells[1].get('fmt', ''):
            continue
        names = [resolve(frame).get('name', '<unknown>') for frame in cells[6]]
        if not names or (scope and not any(scope in name for name in names)):
            continue
        weight = int(cells[5].text or 0)
        total += weight
        samples += 1
        threads[cells[1].get('fmt', '<unknown>')] += weight
        leaf[names[0]] += weight
        if names[0].startswith('0x'):
            unresolved += weight
        for name in set(names):
            inclusive[name] += weight
    def ranked(counter):
        return [{'name': name, 'cycles': count, 'percent': count * 100 / total if total else 0}
                for name, count in counter.most_common(limit)]
    return {'source': str(path), 'scope': scope, 'thread': thread, 'after_seconds': after, 'samples': samples,
            'cycles': total, 'unresolved_leaf_percent': unresolved * 100 / total if total else 0,
            'threads': ranked(threads), 'leaf': ranked(leaf), 'inclusive': ranked(inclusive)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--scope', default='')
    parser.add_argument('--thread', default='')
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--after', type=float, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(summarize(args.samples, args.scope, args.after, args.thread, args.limit), indent=2)
    if args.output:
        args.output.write_text(result + '\n')
    print(result)


if __name__ == '__main__':
    main()
