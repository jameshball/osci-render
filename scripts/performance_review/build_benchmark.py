#!/usr/bin/env python3
"""Link the benchmark against an already-built macOS Profiling product library.

Reuses the compiler response file and framework list from its xcodebuild log.
Never resaves the project or rebuilds product code. Retain the build intermediates
and use a log containing both PluginProcessor compilation and standalone linking.

From the repository root, after building the Profiling standalone:
  python3 scripts/performance_review/build_benchmark.py --build-log /path/to/build.log
  python3 scripts/performance_review/generate_projects.py --template /path/to/current.osci --output build/performance-review/corpus
  python3 scripts/performance_review/run_matrix.py --manifest build/performance-review/corpus/manifest.json --benchmark build/performance-review/processor_benchmark --output build/performance-review/results

Use --compare-with on run_matrix.py for alternating before/after runs, and
--sample-rates, --block-sizes and --ratios for workload sweeps. Video cases require
--ffmpeg. The runner isolates settings and measures the processor without an editor
or audio device. Use --allocation-probe here for optional macOS allocator counters;
its instrumented timings are diagnostic, not performance comparisons.
"""
import argparse
import pathlib
import shlex
import subprocess


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-log', type=pathlib.Path, default=root / 'build/performance-review/profiling-build.log')
    parser.add_argument('--product', choices=('render', 'motion'), default='render',
                        help='Product library and processor source to link (default: render).')
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--allocation-probe', action='store_true', help='Build macOS allocator interposer and opt-in callback counters')
    args = parser.parse_args()
    lines = args.build_log.read_text().splitlines()
    if not any('** BUILD SUCCEEDED **' in line for line in lines):
        raise SystemExit('Wait for the Profiling product build to finish successfully.')
    product = {
        'render': {
            'processor_source': '/Source/PluginProcessor.cpp',
            'library': '-losci-render',
            'benchmark_source': root / 'scripts/performance_review/processor_benchmark.cpp',
            'default_output': root / 'build/performance-review/processor_benchmark',
            'build_dir': root / 'Builds/osci-render/MacOSX',
        },
        'motion': {
            'processor_source': '/Source/motion/MotionProcessor.cpp',
            'library': '-losci-motion',
            'benchmark_source': root / 'scripts/performance_review/motion_processor_benchmark.cpp',
            'default_output': root / 'build/performance-review/motion_processor_benchmark',
            'build_dir': root / 'Builds/osci-motion/MacOSX',
        },
    }[args.product]
    compile_command = next((shlex.split(line.strip()) for line in reversed(lines)
                            if ' -c ' in line and product['processor_source'] in line), None)
    link_command = next((shlex.split(line.strip()) for line in reversed(lines)
                         if 'clang++' in line and product['library'] in line and ' -o ' in line), None)
    if compile_command is None or link_command is None:
        raise SystemExit('Build log must contain the selected processor compile and standalone link commands.')
    response = next((arg[1:] for arg in compile_command if arg.startswith('@')), None)
    if response is None or not pathlib.Path(response).is_file():
        raise SystemExit('The Xcode compiler response file is missing; retain the build intermediates.')
    output = (args.output or product['default_output']).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    object_file = output.with_suffix('.o')
    cwd = product['build_dir']
    compiler = link_command[0]
    command = [compiler, '@' + response, '-g', '-I' + str(root), '-I' + str(root / 'JuceLibraryCode'),
               '-c', str(product['benchmark_source']), '-o', str(object_file)]
    for option in ('-target', '-isysroot', '-stdlib'):
        if option in compile_command:
            command.extend([option, compile_command[compile_command.index(option) + 1]])
    command.extend(arg for arg in compile_command if arg.startswith('-D'))
    if args.allocation_probe:
        command.insert(2, '-DOSCI_ALLOCATION_PROBE=1')
        probe = output.with_suffix('.allocation_probe.dylib')
        probe_command = [compiler, '-std=c++17', '-O2', '-dynamiclib']
        for option in ('-target', '-isysroot'):
            if option in link_command:
                probe_command.extend([option, link_command[link_command.index(option) + 1]])
        probe_command.extend([str(root / 'scripts/performance_review/allocation_probe.cpp'), '-o', str(probe)])
        subprocess.run(probe_command, cwd=cwd, check=True)
    subprocess.run(command, cwd=cwd, check=True)
    command = [compiler]
    i = 1
    while i < len(link_command):
        arg = link_command[i]
        if arg in ('-filelist', '-o'):
            i += 2
            continue
        if arg == '-Xlinker' and link_command[i + 1] == '-object_path_lto':
            command.extend(['-Xlinker', '-object_path_lto', '-Xlinker', str(output.with_suffix('.lto.o'))])
            i += 4
            continue
        if arg == '-Xlinker' and link_command[i + 1] == '-dependency_info':
            i += 4
            continue
        if arg == '-Xlinker' and link_command[i + 1] == '-no_adhoc_codesign':
            i += 2
            continue
        command.append(arg)
        i += 1
    command.extend([str(object_file), '-o', str(output)])
    subprocess.run(command, cwd=cwd, check=True)
    subprocess.run(['xcrun', 'dsymutil', str(output)], check=True)
    print(output)
    if args.allocation_probe:
        print('Run with DYLD_INSERT_LIBRARIES=' + shlex.quote(str(probe)) + ' ' + shlex.quote(str(output)))


if __name__ == '__main__':
    main()
