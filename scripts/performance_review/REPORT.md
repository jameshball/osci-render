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

## Follow-up: modulation curves and compressed audio

A narrower graph-node search excludes endpoints already handled by the boundary checks and removes a redundant guard. Direct isolated comparison passed 716,906 bit-identical values, including duplicate nodes, boundary-adjacent floats, curved segments and smoothing. The real-processor comparison completed 192 successful launches with zero nonfinite samples: 16 cases, six before/after pairs, 48 kHz and 256-sample blocks. Case median paired mean changes ranged from −3.05% to +2.41%, with a median across cases of −0.62% and substantially wider individual-pair variation. Retain this small simplification without claiming a substantial general speedup. Prepared segment coefficients added little over narrowed search for default triangle/sine curves in the isolated probe, while requiring cache invalidation across waveform edits, restores and undo; that additional complexity is not justified yet.

Only 66/96 paired output hashes matched. Geometry output varied within both executables across launches, while defaults, both Lua cases and WAV matched every pair. This batch therefore does not establish full processor output equivalence. Custom 3/8/128-node curves were evaluated by all eight LFOs in every callback, but had no assignments: valid evaluation-cost stress, limited audio-output validation. A separate active-curve corpus assigns all eight LFOs to a scale effect on a deterministic WAV source; its completed results are recorded below. Reproduce these checks with:

```bash
python3 scripts/performance_review/check_graph_curve.py --run
python3 scripts/performance_review/generate_curve_stress.py \
  --manifest build/performance-review/reproduction/corpus/manifest.json \
  --output build/performance-review/reproduction/active-curves
```

The maintained lookup oracle extracts current production interpolation and compares the lookup with a linear search. It does not independently validate the interpolation formula. Run the generated active-curve manifest with the matrix runner and retained before/after binaries.

A separate M4A experiment compared streaming, full predecode, and JUCE buffering with native/resampled playback, seeks, loops, nonlooping ends and rate changes. For 2/60/120-second stereo 44.1 kHz files, full predecode setup cost approximately 1.1/16–18/30–34 ms and added 0.7/21/42 MB of PCM payload, versus streaming setup around 0.5/0.6/0.9 ms. These are warm setup observations, not cold-disk timings or total process RSS. The compressed source remains retained. Existing file/effect locks span parsing, so synchronous predecode would extend that critical section as well as increasing memory use.

Actual WavParser-versus-WavParser controls were bit-exact. The separately compiled streaming replica differed by up to 1.12e-7; predecode and awaited buffering additionally differed around decoder seek/loop boundaries, reaching approximately 0.001. JUCE buffering used a 262 KB buffer and took approximately 8–16 ms to prepare; immediate transition reads showed substantial missing-audio differences. Its callback locks, preparation sleeps and underrun/offline behavior prevent treating it as a simple realtime-safe replacement. Neither alternative was adopted. Removing compressed decoding from the callback remains worthwhile, but requires an explicit memory budget, safe loading/publication and transition semantics rather than a speculative local substitution.

Local follow-up evidence is in `curve-probe`, `curve-followup/{summary.json,summary.md,paired}`, `decoder-followup/*-stage-results.jsonl` and `followup-decoder-curve.md`. The latter records decoder fixture commands and limitations. Experimental decoder replicas are not promoted to maintained correctness tests because they are not bit-identical to the existing pipeline. No raw recordings, generated projects or benchmark result files are committed.


## Follow-up: visualiser Lanczos resampling

The visualiser's existing Lanczos filter uses radius 8 (16 taps), a 4,096-row interpolated coefficient table and a fixed 6× ratio. Sharing the coefficient calculation across synchronized channels preserves each channel's original SIMD accumulation order. Six alternating production-flag microbenchmark pairs measured 800-input processing at approximately 32→21 µs for two channels, 48→26 µs for three, and 80→38 µs for five. One channel remained approximately 15.5 µs. Five-channel 3,200/6,400-input cases improved from 319/621 to 156/302 µs. These are resampling timings, not whole-visualiser CPU or frame-rate improvements.

The comparison checked 45,586,569 bit-identical samples; another eight-ratio check covered 24,655,766 outputs. The renderer integration's independent historical reference passed ASan/UBSan across 1,500 variable blocks, 161 mode/sweep/enable transitions and 21,171,647 output frames. The final strengthened check used distinct signals and independent reference state for each channel, plus an independent sweep interpolation oracle: 70,332,807 scalar channel comparisons passed on ARM under ASan/UBSan and on x86 AVX. An earlier x86 SSE check also passed. Reset now resets the circular write position as well as phases/history: retaining the old position could introduce differences after inactive channels resumed. Active channels reset together when their grouping changes.

The renderer previously assumed every call produced exactly input-count × 6 samples. The first 800-input call actually produced 4,747 rather than 4,800, and occasional later chunks produced one extra output. Output and GL geometry storage now allow the documented per-chunk extra sample, and drawing uses the actual returned count. This removes stale initial tails and a potential buffer overrun. Sweep X still uses linear interpolation to preserve sharp resets. No render semaphore, repaint-trigger or acquire logic was changed.

A separate offscreen GPU prototype used the same table, phase descriptors, taps and reduction ordering. Ordinary GLSL arithmetic differed by up to 2.38e-7; GLSL 4.10 `precise` produced zero differing bits over 274,429 test samples on this M3 Pro. That does not establish equivalence across drivers. Keeping output on the GPU cost about 79 µs for an 800-input workload, versus the improved CPU's 21–38 µs for two to five channels. Larger five-channel workloads were more promising against the original CPU, but the shared CPU implementation was faster than that GPU prototype too. Readback added further cost. A production backend would require GPU history/phase storage, new buffer consumption in the line renderer, capability gating and fallback. It was not adopted.

Reproduce the renderer numerical and bounds check with `python3 scripts/performance_review/check_visualiser_resampling.py --sanitize`. The reference comes from the recorded historical dependency commit, not the optimized single-channel path. Local microbenchmark/GPU evidence remains in `build/performance-review/resampling-followup/`. Shader-stage and macOS resize investigations are separate from these resampling measurements.

## Follow-up: macOS editor responsiveness

Paced background window resizing exposed substantial message-thread delays. This measurement uses one persistent Python process, one local TCP request per event, and Jucewright `snapshot:false` acknowledgments. Earlier measurements that serialized an accessibility snapshot after each event were rejected: that snapshot work dominated their profiles. Requests resize the native window programmatically; they do not reproduce macOS's interactive live-resize path or measure visible-frame presentation.

The Profiling-only `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1` candidate was compared with a preserved flag-0 app using fresh copies of the same muted, input-disabled profile. Each condition had three alternating repetitions of 10 seconds of sinusoidal resize requests at a target 60 Hz. Slow requests reduce the achieved rate rather than causing catch-up bursts. The table reports the median of each run's percentile, in milliseconds:

| Condition | Flag 0 p50 / p99 | Flag 1 p50 / p99 |
| --- | ---: | ---: |
| Visualiser paused | 36.24 / 76.49 | 9.92 / 29.09 |
| Visualiser playing | 40.81 / 81.09 | 13.65 / 33.49 |

Across the three repeats, paused p50 ranged from 30.85–36.57 ms with flag 0 and 9.42–9.93 ms with flag 1; paused p99 ranged from 74.41–76.74 ms and 27.37–29.53 ms respectively. Playing p50 ranged from 38.85–41.51 ms and 12.24–13.77 ms; playing p99 ranged from 78.17–81.47 ms and 30.71–34.36 ms.

The paused comparison more directly isolates editor rendering: the later app also contains a visualiser CPU-resampling change that can affect playing results. Saved project pause states were checked after each run. A separate 16-second flag-1 idle ping run at 120 Hz completed 1,919 requests, with p50 1.01 ms, p99 5.79 ms and maximum 11.14 ms; none exceeded 33 ms. This isolated profile did not reproduce the user's periodic idle stutter or test contention with other instances' settings writes.

JUCE's flag changes how CoreGraphics drawing reaches the backing layer, preserving separated dirty regions instead of relying on macOS's consolidated display-list drawing. It still uses CoreGraphics for component painting. Native interactive live resize uses a distinct synchronous presentation path; compatibility with embedded OpenGL, transparent popouts and fullscreen also needs visual validation before treating this as a generally safe shipping change. No native drag improvement is claimed. Background held-mouse experiments were rejected because native covered-window hit testing prevented the source drag from starting. The candidate flag was removed from the tracked Profiling configuration so profiling continues to represent the normal renderer. Enablement is deferred pending native interaction and visual compatibility checks; the preserved flag-1 app permits that check without rebuilding.

A separate small cleanup removes synchronous beta-update settings refreshes from resize/layout and marks the editor opaque, matching both products' full-background paint methods. Constructor and explicit update-settings callbacks still refresh the badge. Another instance's beta-track change is consequently no longer discovered incidentally when resizing this editor; reopening the editor or its settings refreshes it. These cleanup changes were not included in the flag comparison, and no separate speedup has yet been measured for them.

To obtain an isolated session through the existing input-disabling preparation path, run `python3 scripts/browse_osci_render_with_jucewright.py --quick --keep-app --session ui-measure --app /absolute/path/to/osci-render.app`. This executes UI coverage and leaves the app open; it is separate from timing and should be run when UI automation is appropriate. Establish the desired muted/project/paused state before measuring. Do not launch directly with a copied profile that has bypassed `disable_profile_audio_input`.

For that existing session, replacing `SESSION` with `ui-measure`, reproduce the request protocol with:

```bash
python3 scripts/performance_review/measure_ui_responsiveness.py \
  --session SESSION --mode resize --seconds 10 --hz 60 \
  --size 1100 770 --amplitude 100 70 --output /absolute/path/resize.json
python3 scripts/performance_review/measure_ui_responsiveness.py \
  --session SESSION --mode ping --seconds 16 --hz 120 \
  --output /absolute/path/ping.json
```

The tool does not launch or focus an app and restores the supplied base window size after resizing. Use distinct output paths and the same profile/paused state for each variant. Results contain timing/configuration metadata but no session authentication token. Local evidence and exact test PIDs are retained under `build/performance-review/ui-followup/metal-ab-*`; preserved flag-0/flag-1 apps remain local.

## Follow-up: visualiser shader stages

Four temporary stage-instrumentation captures each collected 300 new-audio frames after 60 warmup frames, using isolated muted profiles at a requested 96 kHz and 60 visualiser frames/second. Cases were 1,024-square Empty and Vector Display canvases with upsampling, a 2,048-square Empty canvas, and 1,024-square Empty without upsampling. All captures completed with zero pending frames and zero ring-buffer drops. The M3 Pro driver exposes 32-bit `GL_TIME_ELAPSED` counters but zero bits for `GL_TIMESTAMP`; the instrument used sequential elapsed queries, checked availability before reading, and never waited for query completion.

For the 1,024-square Empty case, median CPU render-scope time was 2.81 ms. Query-bracketed elapsed times were approximately 0.83 ms for line drawing, 1.00 ms for tight blur, 0.88 ms for wide blur and 1.01 ms for output composition. At 2,048 square, line and composition scopes were approximately 1.77 and 1.62 ms. **These are not shader arithmetic/busy-time measurements:** they include driver submission and GPU scheduling between query boundaries, and instrumentation can change command batching. Do not use them as shader utilization percentages or add them to CPU time. These were individual captures, not paired optimization comparisons. Most stages had no zero query results; the Vector Display glow stage had 15/300 zeros, so its positive-only statistics are conditional on driver resolution.

A candidate simplified the line/output shaders' radial fisheye transform from polar conversion (`atan`, `sqrt`, `sin`, `cos`) to the algebraically equivalent `uv * (1 ± strength * dot(uv, uv))`. Offscreen tests compiled the actual production fragment shaders, with seeded HDR line/screen/glow textures, opaque and transparent output, strengths 0 and 0.5, and 512/513-square targets (including the exact center). Zero-strength outputs were bit-identical. At strength 0.5, the worst raw RGBA difference was approximately 0.00317; only 4–12 of roughly one million channels per case changed after 8-bit quantization, and none changed by more than one quantization level. This illustrates how tiny UV differences can be amplified at texture edges. Nonfinite UV inputs do not have an equivalence guarantee.

Five warmed batches of 64 offscreen draws per variant measured output composition around 4.5–6 µs per draw with no meaningful improvement; the line fragment probe changed from approximately 2.66 to 2.47 µs. These isolated throughput measurements do not represent the entire application's draw workload, but demonstrate why the millisecond query scopes cannot be interpreted as arithmetic cost. The negligible absolute benefit did not justify changing rendered output: **production shaders remain unchanged**. Pairing Gaussian texture taps or changing render-target precision would also alter sampling/rounding; neither was introduced without evidence of a worthwhile benefit.

Temporary stage hooks and their absolute helper includes were removed byte-for-byte before final builds. Local evidence is under `build/performance-review/shader-stages/` and `build/performance-review/resampling-followup/`: `stage-summary.jsonstream`, `fisheye_gpu_512.csv`, `fisheye_gpu_513.csv`, `fisheye_math.log`, and their probe sources. The retained resampling microbenchmark uses `final_api_bench.cpp` with Apple Clang `-std=c++20 -O3 -flto -fstrict-aliasing -arch arm64`; its final results are `final_api_single_paired.csv`, with additional ratio results in `ratios.log`. GPU Lanczos fidelity results are in `check_precise.log`, and retained-output/readback comparisons in `bench-repeat-*.csv`. These local experiments are not required runtime code or dependencies.

### Completed active-curve and reduced raster-cache comparisons

The active-curve follow-up completed 72 successful launches: six 3/8/128-node smooth/nonsmooth workloads, six before/after pairs each. Every launch verified one enabled effect and eight LFO assignments, and all **36/36 paired channel hashes matched exactly**. Median paired mean changes remained mostly small; the three-node smooth case improved approximately 1.8%, while the 128-node nonsmooth case was approximately 3.0% slower with a wide pair range. This provides direct active-modulation equivalence evidence, without establishing a substantial universal curve-search gain or justifying coefficient-cache complexity.

The reduced 3,072-byte raster cache completed 240 successful launches across 20 normal/inverted, static/modulated workloads. Expected effects, actual modulation assignments and configuration metadata matched; all runs remained finite. Median paired callback gains were approximately **36–38% for static PNG, 46–61% for JPEG/JPG, 19–36% for GIF and 5–27% for MP4**. Modulated PNG remained a tradeoff: normal/inverted variants were **3.2%/4.3% slower**, with all six pairs slower in each case. Median paired increases were approximately **3.6/5.5 µs per 256-sample callback**; median run means were 110.7→114.4 µs and 125.9→132.9 µs, respectively. The simple 3 KB cache is retained for the much larger improvements elsewhere, with this regression documented. The larger-cache batch had normal PNG roughly flat and inverted PNG approximately 5% slower; separate batches do not establish that reducing the cache fixed this regression.

The PNG has 250 grayscale intensities and only 46 white pixels among 4,194,304; inversion produces no exact-white pixels. A white-pixel fastpath would not explain or address both regressions. A separate scalar probe using that histogram found approximately 99.7% cache hits with static powers and an 84–85% predicate speedup. Changing the exponent every 1–128 calls yielded 0–42% hits and an 11–17% predicate slowdown from cache lookup/miss overhead. This explains a plausible tradeoff; it does not measure real traversal hit rates or spatial locality. No adaptive policy was introduced.

Raster hashes are intentionally not an equivalence claim: independently seeded traversal produced 0/120 identical pairs. Direct correctness checks instead passed 1,024,000 production predicate/RNG comparisons and eligible threshold-bit comparisons. The graph lookup oracle passed 716,906 values. Run `python3 scripts/performance_review/check_raster_threshold.py --run` to repeat the raster oracle. Final local summaries are `raster-followup/summary-v2.{json,md}`, `raster-followup/micro-summary.json` and `curve-followup/active-summary.{json,md}`; full timings remain uncommitted.

The final app restores the normal flag-0 renderer; the Metal flag remains an unshipped candidate in the preserved local app. A separate cleanup comparison used three alternating paused runs per variant with the maintained responsiveness runner (10 seconds, target 60 Hz). Old versus final median-run p50 was 31.98 versus 29.72 ms, with ranges 30.50–34.19 and 25.55–31.72 ms; p99 was 77.24 versus 74.80 ms. Median maxima were 81.64 versus 83.70 ms. The overlapping distributions and unchanged long delays do not establish a significant stutter fix. These results support retaining the low-code cleanup without claiming that it resolves the reported native interaction problem.

The final normal-renderer app's 16-second, 120 Hz ping completed 1,894 requests: p50 0.813 ms, p99 9.535 ms and maximum 17.844 ms, with none above 33 ms. Audio settings, recording settings and About each opened with their expected content and dismissed to zero visible dialogs. Validation used JSON accessibility snapshots, not GL screenshots or native foreground interaction. All six comparison profiles retained the requested paused state after exit. The maintained runner completed every resize pass and the final ping; exact isolated test PIDs were closed afterward. Evidence remains in `ui-followup/cleanup-ab-*` and `ui-followup/cleanup-final-smoke/` under the local performance directory.


## Final follow-up validation

The final audio executable passed another 144 general corpus projects at 48 kHz/256 samples/1×. The distinct-channel renderer oracle described above passed, as did ASan/UBSan runs of the 716,906 curve and 1,024,000 raster predicate/RNG checks. The final osci-render Profiling standalone and VST3 builds passed without the temporary shader hooks or experimental macOS painting flag. Pluginval strictness 5 passed at 44.1/48/96 kHz and 64/127/512 samples with GUI tests disabled.

An unused vertex scratch vector served only as an initialized/not-initialized flag. Replacing it with an explicit atomic readiness flag removes approximately 230 KB per renderer at 48 kHz/60 fps/6× without changing GL call timing or buffer contents. This is not a general thread-safety fix. A subsequent unpaused embedded-view resize and Audio/Recording/About open-dismiss smoke pass succeeded in the rebuilt app. No native presentation or screenshot validation is implied.

The proposed next investigation is recorded in [Integrated GPU visualiser follow-up](GPU_RENDERER_FOLLOWUP.md). It begins with packed point buffers and instanced segments while retaining CPU Lanczos, then makes GPU convolution a separately measured decision. It includes exactness requirements, ownership/fallback risks and whole-frame acceptance gates; no integrated GPU backend is implemented here.


The final sosci Debug standalone build also passed after its own Projucer resave. An isolated muted, input-disabled, unpaused embedded-view smoke test completed a short resize and Audio/Recording/About open-dismiss checks; popout/demo and system-audio capture stayed disabled. All test app PIDs were closed. Generated headers were then restored for osci-render, and formatting-only Projucer changes were discarded after semantic XML comparison. No full final Windows/Linux application pass or native macOS drag/live-resize pass is claimed.


### Modulation-panel seam shadow during resize

The modulation panel generated and blurred a full-height rounded panel shadow, then clipped its displayed result to a 10-pixel tab seam. Changing panel height therefore invalidated and rebuilt the entire shadow image. The shadow-only source bounds now retain at most 20 pixels of height, leaving the lower corners outside the visible seam's blur support. The existing path topology, shadow radius, offset, opacity and final clipping remain unchanged; short panels retain their original height.

An offscreen helper compared 945 combinations of widths (1–641), panel heights (1–500), scales (1/1.25/1.5/2/3) and active-tab exclusions. All output pixels matched exactly. An earlier plain-rectangle candidate differed by one alpha unit in 40 pixels at 1.25× and was rejected; retaining the original rounded-path construction eliminated those differences.

Six alternating repetitions of 120-frame offscreen trajectories at 2× scale measured median mean paint costs of 993.6→182.3 µs for width changes, 973.3→11.5 µs for height changes, and 1058.2→186.7 µs when both dimensions changed. Static cost was 17.8→11.3 µs. Both variants include the same output-image allocation; the height-only candidate reuses its cached shadow. These are isolated shadow-paint measurements, not whole-window latency, native presentation or evidence that the separately reported periodic modulation-drag freezes are fixed. The helper, compiler log and comparison output remain in `build/performance-review/graph-shadow-followup/`.

The shadow-only macOS Profiling standalone build passed with the normal renderer and external desktop drag badge retained. Its separate app output is under `graph-shadow-followup/app/`; no running user or comparison application was restarted.


### Native modulation-drag freezes: September 6 follow-up

User screen recordings distinguish slow native resizing with expanded modulation graphs from intermittent drag freezes. A 60 Hz frame-difference check of a fixed graph region in the drag recording found five candidate animation holds of 350–500 ms; this identifies visible pauses, not their cause.

A 45-second Time Profiler capture of the normal Profiling app recorded eight 497–501 ms hangs plus a 745 ms pause near capture startup. An isolated muted comparison using the previously preserved multiple-paint-calls app still recorded six 498–501 ms hangs; the user reported dragging and resizing felt unchanged. These were manual interaction captures, not matched trajectories, and the comparison app predates the final small cleanups. The alternative painting flag remains disabled.

The next drag-only capture recorded one 501 ms hang. A custom Time Profiler template requested waiting-thread samples, but the exported schema still reported `record-waiting-threads=0`; that experiment did not provide the intended blocked-thread timeline. A subsequent System Trace supplied syscall and scheduling evidence. Its default windowed recording retained ten seconds of the requested 45-second session, containing three complete ~498 ms hangs and a fourth interval truncated at the recording boundary. Scope statements refer to that retained interval.

In each complete System Trace hang, the main thread waits in `__ulock_wait2` → `SLSConnectionSetLastSLSCATransaction` while committing an AppKit/Core Animation transaction. Overlapping it, the OpenGL renderer waits in `mach_msg2_trap` → `_SLSTransactionWaitSource` → `SLSConnectionSynchronizeSLSCATransaction` → `SLSWindowIsOrderedIn` → `CGLFlushDrawable`/`NSOpenGLContext::flushBuffer`. One pair measures 500.218 ms in OpenGL presentation and 492.979 ms in the UI-thread lock wait, ending together. This narrows the periodic freeze to native presentation/window-transaction synchronization rather than expensive modulation evaluation. The initial aggregate profile's CoreGraphics queue costs alone were not sufficient to identify this link. Heartbeat settings writes were a small sampled cost, not the demonstrated half-second blocker.

A [JUCE forum report](https://forum.juce.com/t/juce-and-macos-tahoe-ui-performance-issues/67953/3) independently describes intermittent Tahoe stalls when drag images use external windows. Changing the modulation drag to an in-editor component was considered and reverted: the badge must remain above the native OpenGL visualiser. The external-window argument is retained. The accepted fix enables JUCE's existing main-thread `BufferSwapper` route on macOS 26 with a one-line predicate change. Rendering remains on the OpenGL thread; only native presentation uses the existing message-thread route. The application rendering semaphore and external drag-window behavior remain unchanged. CI applies the maintained JUCE patch, and local build instructions document the same step.

With this change and the small seam-shadow fix above, a 35-second System Trace and a subsequent 90-second Time Profiler capture each recorded zero hangs. The user could no longer reproduce the freezes during the longer manual modulation-drag test. These are manual comparisons, not identical input trajectories or proof against every intermittent stall. A review of the existing swapper found that pending async updates are cancelled during teardown and its native context outlives it.

A further 55 programmatic popout actions passed, covering transparency toggles, resizing and repeated close/reopen cycles. With the popout present, 492 idle responsiveness requests over six seconds measured p50 7.945 ms, p99 26.346 ms and maximum 31.165 ms, with none above 33 ms. These background checks establish responsiveness and lifecycle coverage, not native fullscreen transitions or rendered pixel fidelity. They have no matched baseline and are not an overall UI performance comparison. Other macOS versions, Intel hardware and plugin-host presentation have not received the same manual interaction test. Raw captures and local analysis remain under `build/performance-review/drag-followup/` and are not published.


Final builds with the maintained JUCE patch passed for osci-render Profiling standalone/VST3 and sosci Debug standalone. Both standalone products passed isolated muted, input-disabled resize and Audio/Recording/About open-dismiss checks. These were functional smoke tests, not comparative native-resize measurements. The sosci test was closed; the final osci-render Profiling test copy remains available. Generated headers were restored for osci-render after the sequential product builds. Logs remain in `drag-followup/final-builds/`, `final-smoke/` and `final-sosci-smoke/` beneath the local performance directory.
