# Performance review — September 2026

Measurements use an M3 Pro MacBook with 18 GiB RAM and the optimized macOS Profiling configuration with matching symbols. The owner continued using the laptop. Small timing differences are not treated as improvements.

## Measurement scope

`processor_benchmark.cpp` runs the real processor in a JUCE standalone application with isolated settings, no audio device and no editor. It measures the outer callback, including internal sample-rate conversion. MIDI/input preparation, output checks, message pumping and serialization are outside timing. Asynchronous voice creation is warmed up and the harness waits, with a timeout, for the exact requested pool before retriggering notes; actual active voices are also checked. One short high-rate warmup exposed an under-filled pool (11/16 active voices), so that run was rejected. Six repeated checks after adding readiness waiting all reached 16 voices, with an additional 46–78 ms setup wait.

The runner validates loaded sources, effects, voices and modulation assignments. Ordinary cases require finite, nonzero output; the zero-length geometry regression permits silence. Missing or malformed measurements fail, and stale output is removed before each launch. Comparisons alternate executable order. Current manifests specify MIDI velocity 80; early runs used approximately 102. Comparisons must use matching inputs. Current per-channel sample-bit hashes are stronger than the older aggregate checksum, but are not a proof against every possible collision. Randomized image paths and asynchronous voice activation can legitimately differ between runs.

CPU Profiler percentages describe sampled CPU cycles, not wall time or GPU cost; inclusive entries overlap. The separate allocation interposer counts wrapped malloc/calloc/realloc/free on the callback thread, excluding free(nullptr). It does not cover every allocation API or LuaJIT's private allocator. Its timings are diagnostic only. Headless presentation metadata does not exercise popouts, fullscreen or paused UI.

## Accepted fixes

| Finding | Evidence and change |
| --- | --- |
| Heavy Lua repeatedly fails to compile machine code | In six alternating pairs, three old launches averaged 16.55–16.80 ms/callback and three averaged 223–230 µs; all six patched launches averaged 222–278 µs with matching aggregate checksums. Slow trace diagnostics recorded over 1.5 million machine-code allocation failures and no completed trace. Pin the official LuaJIT fix, `68354f444728ef99bb51bb4d86e8f1b40853a898`. Six subsequent launches of the pinned dependency averaged 221–227 µs with identical per-channel hashes. |
| Per-voice Lua states leak and stale pointer tracking can confuse source changes | Actual-processor finalization diagnostics exposed missing cleanup. A small RAII state holder and parser generation tokens replace pointer tracking; closing detaches console callbacks before finalizers run. Cleanup, A/B/A switching, reset, fallback and instruction-limit tests pass AddressSanitizer and UndefinedBehaviorSanitizer. |
| Sample-source retriggers destroy unused geometry on the callback | Lua 64-note churn with 16 voices produced 964 frees over 512 callbacks; skipping unused geometry initialization reduced this to zero. Held-note Lua/SVG/Lua transitions passed with 1/16 voices and 1×/4× internal rates. Envelope and note-trigger behavior is retained. |
| Increasing polyphony can lose the kill-fade overlap voice | With the actual processor, requesting one voice produced a pool of two, then requesting two incorrectly left the pool at two. Always publish the new builder target outside the telemetry condition. The unchanged fixture now observes pools of two then three. |
| A zero-length frame hangs shape traversal | A valid 400-byte GPLA with identical vertices timed out; its nonzero control completed. All 312 sampled main-thread stacks were in the traversal loop. Guard empty/nonpositive-length frames; generate both fixtures as lasting regressions. |
| Idle object-server restart adds a 200 ms restore tail | Fifty interleaved runs showed roughly 4–205 ms for old small/default/Lua restores versus 1–2 ms after avoiding unnecessary listener restarts. PNG restoration remained roughly 100 ms before the separate bitmap-access change below. Restart after releasing processor locks when the port changes, the listener stopped or a client is active. |
| Object-server framing and queue shutdown mishandle lifecycle boundaries | Split/coalesced input, EOF, oversize, reload and shutdown fixtures cover the listener changes; app checks cover active-client reload, port changes and bind-failure retry. A separate queue regression proved cancelled operations could overwrite frame ownership. Check cancellation before mutation and update the wait predicate under its existing mutex. |

The LuaJIT diagnosis matches the [upstream report](https://github.com/LuaJIT/LuaJIT/issues/1280) and [official fix](https://github.com/LuaJIT/LuaJIT/commit/68354f444728ef99bb51bb4d86e8f1b40853a898). Trace evidence ruled out the initial instruction-hook hypothesis. This removes a catastrophic slow path, without demonstrating a general speedup when the old JIT already worked.

## Validation recorded

Completed coverage includes:

- The final executable passed all 144 corpus-v4 projects, all 128 dense-shape configurations (16 workloads × 44.1/192 kHz × 127/512 samples × 1×/4×), and 108 alternating source/effect regression runs.
- The pinned dependency passed all 128 corpus-v3 projects at 48 kHz, 256 samples and 1×, plus 42 configurations at 192 kHz with 127/512-sample blocks and 1×/2×/4× rates. Earlier coverage included 64 configurations at 44.1/96 kHz, 64/1024 samples and 1×/2×/4×/8×.
- Forty additional edge-rate cases passed for corpus-v4's envelope/random/sidechain modulation and zero/control geometry. Sidechain uses explicit nonzero host input. Source families include OBJ, SVG, text, Lua, GPLA, images, Lottie, fractals, audio and video; AAC/M4A fixtures are macOS-only.
- The final macOS Profiling standalone and VST3 builds passed. ASan/UBSan categories Lua, Concurrency, Synth, Audio, LFO and BufferConsumer passed against the final production changes. Pluginval strictness 5 passed at 44.1/48/96 kHz and 64/127/512 samples with GUI tests disabled. Generator selftests, mocked runner checks, the traversal oracle and isolated listener/lifecycle checks also passed.
- The official LuaJIT pin built and executed a compiled-trace smoke test on Linux ARM64; macOS ARM64 and x86_64 compiled-trace smoke tests also passed. This is dependency validation, not a new full Windows/Linux application test pass.
- Some combinations exceeded their callback deadline: the high-rate matrix reached a mean of 33.94 ms. Passing signal/configuration checks does not imply realtime capacity at every setting.

## CPU attribution and rejected experiments

Valid source-specific profiles cover geometry, text, raster images, WAV/FLAC and GIF/MP4/M4A. Zero-callback-sample captures were rejected and retried. Geometry profiles place roughly 15–25% of sampled callback cycles in the principal graph-curve evaluation entry, with effect processing and buffer accesses also material. The PNG profile attributes 30.4% to `pow` and 11.0% to nearest-neighbour search. These observations identify costs, not automatically worthwhile optimizations.

A separate GPLA/16-voice/26-effect trace contains 36,097 callback samples: shape traversal accounts for 14.1% of sampled cycles and total-length calculation 2.7%. The later dense high-frequency workload attributed 86.1% of 11,035 callback samples to traversal, motivating the indexed change below. A final capture of the same workload contains 15,832 callback samples (0.18% unresolved leaves); traversal accounts for 16.6% of sampled cycles after the change. These percentages have different total costs, so the paired callback timings below establish the absolute improvement.

Relaxed atomic stores showed no consistent gain in 70 paired runs and were reverted. A later profile-motivated buffer-pointer cache plus early dark-pixel rejection also failed to demonstrate convincing gains in 132 runs across 11 sources, and both were reverted. Most median differences were near zero; apparent teapot improvement varied from roughly −12% to +18% between pairs. A separate visited-pixel-first check was also reverted after 48 runs: video improved by roughly 13–16%, but JPEG regressed by 5–8% and PNG slightly regressed. It also changes PRNG consumption.

In contrast, reusing one read-only bitmap view and traversing rows during still-image loading improved all six paired runs per source: PNG median restore time fell from 98.4 to 83.3 ms, JPEG from 53.0 to 34.7 ms, and JPG from 54.8 to 35.5 ms. The old/new pixel conversion produced identical bytes for RGB, ARGB and single-channel images, native/software backends, clipped views and a real PNG. The change adds one bitmap view and reorders the existing loops. The original load trace contained 202 loader samples (29.8% unresolved leaves), including bitmap initialization and per-pixel access.

## High-frequency shape traversal

The existing length pass now records one double cumulative endpoint per shape (eight extra bytes per shape, no separate allocation). Traversal walks up to 32 nearby edges, then uses binary search and modulo for larger skips. The existing float frame-length calculation and frame-transition behavior are retained. Independent checks extract the production method and compare it with the old linear algorithm: 220,516 checks passed, including 200,000 consecutive variable-frequency advances. There were no shape-index or frame-clock mismatches; maximum normalized interpolation error was 1.16e-11. Floating-point output is not claimed bit-identical.

Six alternating real-processor pairs per workload produced these medians at 48 kHz/256 samples:

| Input polygon / frequency | Before callback | After callback |
| --- | ---: | ---: |
| 4,096 segments / 20 kHz | 1,360 µs | 93 µs |
| 4,096 segments / 4.2 kHz | 331 µs | 93 µs |
| 4,096 segments / 220 Hz | 92 µs | 92 µs |
| 256 segments / 4.2 kHz | 185 µs | 188 µs |
| 256 tiny segments / 220 Hz | 2,606 µs | 1,177 µs |

The dense 20 kHz improvement was 92.9–93.3% in all six pairs. Small four-edge cases remained around 39–42 µs, with differences of a few microseconds; this is not a universal speedup claim. Pure binary lookup had clear small/medium-case regressions and was replaced with the bounded linear prefix. A focused crossover measurement supported the 32-edge limit before repeating the application comparisons. Input segment counts describe the generated GPLA fixtures.

## Remaining costs and limits

M4A decoding produced 546 malloc/free pairs over 512 callbacks. The captured stack reaches AudioToolbox MP4 packet handling through CoreAudioReader and WavParser. Batching cannot guarantee removal of decoder allocations. Predecoding costs about 23 MB/minute for stereo float audio at 48 kHz plus loading time; background buffering adds scheduling, locking and underrun behavior. No supposedly transparent remedy was introduced into the shared realtime/offline parser.

Embedded visualiser checks measured approximately 0.63–0.66 CPU cores playing and 0.50 paused. A later background app profile attributes 51.7% of sampled cycles to the main thread, 18.7% to the OpenGL renderer and 15.6% to VisualiserRenderer. The paused trace loses the rendering threads; main-thread painting remains substantial. These are CPU observations, not GPU/FPS measurements. No rendering synchronization changes were made.

Three isolated background startup checks observed UI responsiveness at approximately 1.95 s initially and 0.68/0.78 s subsequently. Their 50 ms polling and CLI overhead prevent precise startup claims; they do not measure first pixels or audio readiness. Synchronous project-restore timing likewise excludes completion of asynchronous file loading. Audio and recording overlays opened and dismissed through Jucewright in the isolated app. An overlay CPU trace contained 13,325 samples (1.2% unresolved leaves), mainly native painting and allocation; it included overlapping overlays and is not a controlled single-overlay comparison. No UI optimization was accepted. Repeated load timings showed the large WAV and first MP4 delays were outliers (WAV later 10–20 ms, MP4 24–30 ms); their cold-start cause was not proven.

Raw evidence is local under `build/performance-review/`: `luajit-fixed-paired`, `pinned-heavy-lua`, `pinned-corpus-validation`, `pinned-high-rate-validation`, `new-modulation-edge-validation`, `nonlua-paired`, `source-profiles*`, `background-*-summary.json`, `background-startup`, `bitmap-paired`, `shape-final-paired`, `shape-profile-final`, `ready-corpus-validation`, `ready-shape-rate-validation`, `ready-regression-paired`, `ready-asan-*.log`, `ready-pluginval.log` and `ready-linux-luajit.log`. It is not committed with the tooling.

## Reproduction

Run from the repository root on macOS with the recorded submodule revisions, JUCE/Projucer, Xcode and FFmpeg available. Retain a successful **Profiling** standalone build log containing both the `PluginProcessor.cpp` compile command and the final standalone link command; `build_benchmark.py` reuses that build's library, response file and framework flags. An incremental log without those commands is insufficient. Build each comparison revision separately and retain its executable before rebuilding another revision.

```bash
mkdir -p build/performance-review/reproduction/home
python3 scripts/performance_review/build_benchmark.py \
  --build-log /absolute/path/to/successful-profiling-build.log \
  --output build/performance-review/reproduction/processor_benchmark

HOME="$PWD/build/performance-review/reproduction/home" \
CFFIXED_USER_HOME="$PWD/build/performance-review/reproduction/home" \
JUCEWRIGHT_AUTOMATION=1 \
  build/performance-review/reproduction/processor_benchmark \
  --export-default "$PWD/build/performance-review/reproduction/default"

python3 scripts/performance_review/generate_projects.py \
  --template build/performance-review/reproduction/default.xml \
  --output build/performance-review/reproduction/corpus \
  --seed 0 --count 144 --ffmpeg "$(command -v ffmpeg)"

python3 scripts/performance_review/run_matrix.py \
  --manifest build/performance-review/reproduction/corpus/manifest.json \
  --benchmark build/performance-review/reproduction/processor_benchmark \
  --output build/performance-review/reproduction/results \
  --repeat 1 --warmup 1000 --blocks 2000 \
  --sample-rates 48000 --block-sizes 256 --ratios 1 \
  --ffmpeg "$(command -v ffmpeg)"
```

The runner creates its own isolated settings directory. Do not benchmark while building or running other measurements. Codec availability affects generated coverage: inspect the manifest's skipped-format/dependency metadata rather than assuming every encoder is present. Keep the generated corpus unchanged for before/after comparisons. Add `--compare-with /absolute/path/to/after-executable --repeat 6` to alternate executable order, or use comma-separated sample rates, block sizes and ratios for a configuration matrix. Signal/configuration success and callback-deadline performance are separate results.

```bash
python3 scripts/performance_review/generate_projects.py --self-test
python3 scripts/performance_review/check_shape_traversal.py --run
python3 scripts/performance_review/generate_shape_stress.py \
  --manifest build/performance-review/reproduction/corpus/manifest.json \
  --output build/performance-review/reproduction/shapes
```

Run the shape manifest with the same matrix command, replacing the manifest/output paths. For allocation diagnostics, link a separate executable with `build_benchmark.py --allocation-probe`; inject the printed `.allocation_probe.dylib` using `DYLD_INSERT_LIBRARIES` when invoking the matrix runner. Do not use instrumented timings for performance comparisons. The result directory records executable/manifest hashes, configuration metadata, per-launch logs and measurements; preserve it with the exact build log and generated projects.
