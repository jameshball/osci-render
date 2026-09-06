# Integrated GPU visualiser follow-up

This is a proposed investigation, not an implemented backend or a speedup claim. The first experiment should retain the newly optimized CPU Lanczos filter and remove the fourfold CPU vertex expansion. Only move convolution to the GPU if that simpler renderer establishes a worthwhile baseline and a subsequent integrated comparison beats it.

## Current path and measured limits

`modules/osci_gui/visualiser/osci_VisualiserRenderer.cpp` currently prepares samples in `runTask`: it retains the separate audio-output copy, applies visual effects/modulation, flips and coordinate transforms, then resamples the active channels. XY/XYZ/XYRGB filter 2/3/5 channels; sweep mode interpolates X separately, so filters 1/2/4. The existing worker/GL handoff then permits `renderOpenGL` to consume those vectors. This is visualiser work, not a proposal to move the synthesizer or audio callback onto the GPU.

`drawLine` allocates position data and duplicates each `{x, y, brightness}` four times. RGB mode also duplicates each colour four times. Both are uploaded using `glBufferData`. The separate, unused `scratchVertices` allocation has already been removed in favour of an explicit arrays-ready flag; that cleanup does not remove these live position/colour allocations. Adjacent records provide the start/end attributes, a separate index attribute identifies the corner and shutter position, and six element indices draw each segment. Blur, composition, presentation and the completed texture shared through `OpenGLFrameMirror` follow this draw.

The shared CPU filter already reduces isolated 800-input resampling from 31.96 to 20.70 µs for two channels, 47.84 to 26.21 µs for three, and 79.84 to 37.60 µs for five. At 6,400 inputs/five channels it takes approximately 301.61 µs. These are kernel measurements, not whole-frame improvements.

The earlier transform-feedback experiment retained output on the GPU and took approximately 79.5 µs of CPU preparation/submission for 800 inputs/five channels and 451.9 µs for 6,400/five. Diagnostic CPU readback made it substantially slower. It uploaded complete ring snapshots and CPU-generated phase descriptors, dispatched per input chunk, and did **not** feed the real line renderer. It therefore neither beats the improved CPU kernel nor settles the value of an integrated renderer.

Measured versus inferred distinctions:

- CPU kernel time, aggregate prototype CPU preparation/submission, readback time, output errors and uploaded array sizes were measured or inspected directly.
- Descriptor generation, individual uploads and driver dispatch were **not separately timed**. Calling any one of them the dominant bottleneck would be an inference.
- CPU vertex expansion and its allocations are present in production, but their isolated cost and the benefit of removing them have not yet been measured.
- Live GL elapsed-query scopes include scheduling/driver effects and instrumentation can alter batching. They are not pure shader arithmetic time. The isolated fisheye rewrite saved only about 0.19 µs in its line probe; production shaders remain unchanged.

Local evidence is under `build/performance-review/resampling-followup/` (`bench-repeat-*.csv`, precise-output fixtures) and `build/performance-review/shader-stages/`. The maintained CPU oracle is `scripts/performance_review/check_visualiser_resampling.py`; results and measurement limitations are recorded in `REPORT.md`.

## Stage 1: CPU Lanczos, packed points, instanced segments

Create one packed record per output point, initially `{x, y, brightness, r, g, b}`. Keep the existing brightness calculation, including the negative-red colour sentinel, and all filter output unchanged. Use one segment instance with a constant four-corner quad and element order `0,2,1,1,2,3`. Start/end attributes reference adjacent packed records with divisor one. Derive the original global index as `gl_InstanceID * 4 + gl_VertexID`, preserving the current corner and shutter calculations in `LineVertexShader.glsl` rather than simplifying their rounding incidentally.

Instanced attributes advance according to their divisor, which fits this layout. Use a dedicated VAO or restore divisors explicitly so subsequent fullscreen passes cannot inherit segment state. Verify the live context and JUCE function availability rather than assume support from the shader version. [Khronos instanced-array specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_instanced_arrays.txt)

A texture-buffer variant can fetch start/end points directly in the vertex shader. Compare it only if it materially simplifies integration or avoids an attribute limitation: shader fetches are not automatically cheaper. Prefer an indexed four-vertex quad over six independent vertices, which would increase vertex shader invocations. Keep fragment shaders, triangle order, nominal `nEdges` shutter normalization, actual output count, brightness, colour, additive blending and final texture interfaces unchanged.

Payload arithmetic, not measured bus traffic, illustrates the opportunity. For N=800 inputs and approximately M=4,800 outputs:

| Per-frame data | Current | Fixed six-float packed record |
| --- | ---: | ---: |
| XY position upload | 230,400 bytes | 115,200 bytes |
| RGB position plus colour upload | 460,800 bytes | 115,200 bytes |

A separate three-float non-RGB record could reduce XY further, but start with one layout unless measurements justify another shader/layout variant. This stage can be accepted independently; it does not depend on a GPU resampler.

## Stage 2: GPU convolution into that same point buffer

Use a transform-feedback vertex pass with one invocation per output point, calculating the synchronized active channels together and writing the same packed record. The following instanced draw consumes it directly, without CPU readback or expanded position/colour arrays. Basic transform feedback is sufficient; do not accidentally require newer compute/SSBO APIs or transform-feedback object conveniences merely because the prototype used them. Transform feedback provides a buffer-based output path for subsequent rendering. [Khronos transform-feedback specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_transform_feedback.txt)

Upload the exact CPU-generated coefficient and delta tables once per GL context. Keep phase/count computation on the CPU initially. All upstream effects and transforms stay where they are. Sweep X must retain its current linear interpolation and sharp reset, paired with the actual Y output count; it must not silently become Lanczos-filtered.

This stage adds a backend and failure handling, so compare against Stage 1 plus the shared CPU kernel—not the old duplicated renderer. A kernel-only win is insufficient.

## Exactness is a design constraint

The reference is `LanczosResampler<2048, 8>` with sixfold output, 16 taps, 4,096 table positions and the existing wrap row/delta interpolation. Preserve these details:

- Double input/output phase, repeated output-phase addition, per-chunk normalization and the existing split at at most 1,024 inputs. Do not substitute `start + index / 6`, a six-phase cycle, float phase or rounded table indices. They differ at floor/table boundaries.
- The exact sample index, table index and interpolation fraction selected by the CPU, including wrap behavior. Generate explicit descriptors before attempting GPU phase calculation.
- Per-channel multiply, lane accumulation and horizontal reduction order. Do not assume a GPU dot product or FMA produces the CPU result.
- Cleared history and write pointer on reset, synchronized active channels, mode/upsampling/sweep transitions, and actual returned counts. A fresh 800-input block produced 4,747 valid outputs, not 4,800; occasional steady blocks produce one extra output per chunk. Removed stale tails are bugs, not fidelity targets.

The GLSL 150 prototype differed in 63,316 of 274,429 samples, with maximum absolute error 2.38418579e-7. GLSL 410 with explicit `precise` accumulation/output produced bit-identical results for that fixture on the M3 Pro. This is one driver/CPU pairing, not a portable guarantee. `precise` constrains expression transformations; it must be applied inside relevant helper functions because precision requirements do not automatically propagate through calls. [Khronos precise-operation rules](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader5.txt)

The production shader path uses GLSL 150; do not raise the application's global requirement just to reuse the prototype. Apple exposes legacy, 3.2 and 4.1 profiles, but profile availability alone does not validate a shader or establish a precision guarantee. Keep the CPU path when the selected context cannot compile and pass the candidate's exactness checks. [Apple OpenGL profiles](https://developer.apple.com/documentation/appkit/opengl-profiles)

GPU double arithmetic is a separate capability described by ARB_gpu_shader_fp64; Apple support/performance for the intended live context was not established by this experiment. Keeping double phase on the CPU avoids depending on it. Even supported GPU doubles would still require proof of the recurrence and rounding behavior. [Khronos double-precision shader specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader_fp64.txt)

Descriptor cost is a central go/no-go test. The prototype used a 16-byte descriptor per output point. At N=800, five channels, uploading only new inputs plus these descriptors would still mean approximately 92,800 bytes/frame, versus 115,200 for Stage 1's packed CPU output. That is only a calculated 22,400-byte difference, before dispatch and GPU output writes. Packing descriptors or generating phase on the GPU adds complexity and must earn its place independently. Test against the actual SIMD/reduction behavior on each supported CPU architecture; M3 equality alone does not prove x86 equality.

## History, memory and ownership

The prototype uploaded a complete mirrored 4,096-float history per channel: 16 KiB/channel/chunk. Five channels plus 4,800 descriptors meant 158,720 uploaded bytes for a normal frame. The coefficient and delta tables together occupy 524,416 bytes. These fixed tables are small, but repeatedly uploading full history is a correctness baseline, not the proposed steady path.

Two streaming options deserve a bounded prototype:

1. Preserve ordered chunk upload → convolution → next chunk. This is easiest to compare with the CPU, but multiple dispatches and overlapping buffer updates may incur stalls.
2. Upload immutable contiguous frame input plus carry history, mapping the CPU's original virtual-ring indices into that storage. This can reduce dispatches while preserving exact descriptors, but ring-wrap/reset mapping needs independent proof.

Do not upload an entire large frame into a 2,048-position ring before dispatching all outputs: it can overwrite history needed by earlier chunks. Capacity must follow the actual input request, chunk count and checked output bound, including the extra count headroom. Check byte multiplication and GL count/offset limits before allocating or submitting.

The proposed backend must keep all new GL resources, uploads and draws on the GL thread. Before implementation, audit existing `setFrameRate` ownership: its direct `setupArrays` call is reached both from the GL pre-render callback and from offline-render preparation. Static call sites establish a context-ownership risk to investigate, not a proven runtime failure. The arrays-ready flag cleanup preserves current call timing and does not establish general thread safety. The worker prepares immutable input/metadata through the existing handoff. **Do not change the render semaphore, `triggerRepaint()` or acquire/release protocol.** Keep `OpenGLFrameMirror` at the completed-texture boundary; recording/popout consumers need not know about scratch buffers.

Same-context command ordering permits transform feedback followed by drawing from its completed output without CPU readback. If storage is reused across in-flight frames, use bounded slots and nonblocking fence availability checks, with an explicit safe fallback when no slot is free. Never block the audio thread. Buffer orphaning is a simpler prototype option, but bounded application buffer IDs do not imply bounded driver allocations; measure memory and long-run behavior before accepting it.

Retain sufficient CPU state/input to replay the current frame if shader compilation, capacity validation or a context transition forces fallback. Advancing phase once for the GPU and again for fallback would corrupt continuity. A reset-on-failure would introduce a visible transient. Choose one canonical state owner and define when state is committed before implementing the backend.

## Proof sequence and acceptance gates

1. Instrument the current packed-data preparation, allocations, GL upload/submission and total worker/GL frame CPU cost separately. Record actual input/output counts and uploaded bytes.
2. Implement only Stage 1. Compare original and candidate geometry/HDR pixels with sparse and dense lines, zero-length segments, impulse/noise/DC signals, all colours/sentinels, XYZ brightness, sweep, shutter sync, transparency and persistence. Check resizing, popouts and recording. Preserve overdraw/triangle order.
3. Extend the independent Lanczos oracle to the GPU candidate: random chunk boundaries, zero/one/large blocks, ring wraps, repeated resets, enable/disable, all channel modes and sweep changes. Require matching counts and exact filter samples on an advertised exact path. A tolerance relaxation requires an explicit product decision; it is not an implementation shortcut.
4. Compare integrated Stage 2 against Stage 1 at ordinary 48/96 kHz, multiple frame rates, and high-rate/channel stress. Include paused/playing, multiple windows, context recreation and recording. Measure worker plus GL submission CPU, sampled CPU attribution, p50/p95/p99, frame deadline misses, presentation intervals and memory. Include sustained CPU/GPU utilization or energy where tools support meaningful attribution.
5. Report GPU query counter bits, coverage, zeros and dropped queries. This machine exposed 32-bit elapsed queries but zero timestamp bits; short probes sometimes quantized to zero. Never add CPU and GPU elapsed measurements or describe elapsed scopes as shader busy time. Repeat paired runs without instrumentation to detect query overhead.

Accept a stage only for a repeatable, material whole-frame benefit beyond run noise, with no meaningful tail-latency, visual or memory regression. Do not accept hundreds of backend lines to save a few microseconds in an isolated kernel. If Stage 1 captures most of the gain, stop there.

Stage 1 is one focused renderer/layout change with its own visual regression work. Stage 2 spans shader arithmetic, history mapping, descriptor generation, resource lifetime, fallback and platform validation: several distinct reviewable changes, likely substantially more code than the first stage. An implementation estimate should follow the Stage 1 measurements and exactness prototype; the present evidence cannot justify a delivery estimate or promised speedup. A new Metal backend, GPU audio processing, approximate kernels and global GL requirements are outside this follow-up.
