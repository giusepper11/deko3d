# Z24S8 stencil probe — hardware preparation

**PREPARATION PASS — NO HARDWARE EXECUTION.** This record covers one deliberately
narrow, headless correctness executable. It does not approve deployment, establish
GPU correctness, accept the Dawn candidate, or establish safe GPU-hang recovery.

## Source and artifact identities

| Role | Exact identity |
|---|---|
| Initial research HEAD | `d1932746db67e2894c93edefaed3c0c8fc92b802` |
| New implementation | `26bf5fa842d2a498b675fa53ed3455ee1ea1deef` |
| Original experimental implementation, unchanged | `cf53748dd02f007758e1518d8c49610eef117671` |
| Original deko3d base | `373bfd43d8b2d805fa2eb7d406dbb823032df958` |
| Mesa/NVK source reference, not linked | `0ff52079ca9e9e837ed27eb95a8d239f439a1864` |
| Preparation branch | `research/stencil-z24s8-hardware-prep` |
| Toolchain image | `sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c` |
| SDK | GCC15.2.0, devkitA64r29.2-1, libnx4.12.0-1 |

The separate evidence commit is the commit containing this record and
`evidence/z24s8-gpu-preparation-result.json`; it changes no executable source.
The executable embeds the full implementation ID above, not the later docs ID.

Workspace: `/home/gro/dusklight-deko3d/dependencies/deko3d-stencil-hardware-test`.
No original protected worktree was switched to this branch. Existing shared Git
history is retained; the only allowed protected-worktree metadata delta is this
additional worktree registration. The experimental encoder/header/entry-point and
all existing production operations remain byte-identical to the research source.

**Exact candidate for a future approval:**

- NRO: `/home/gro/dusklight-deko3d/builds/z24s8-hardware-preparation/switch-committed/z24s8_stencil_probe.nro`
- Size: **270392 bytes**.
- SHA-256: `c734a74dd67d540cd59b61eaccc89eaac53c20bd318caf0b953865242764e7cd`.
- Linked archive: `builds/deko3d-aspect-copy/cross-committed/libdeko3d-aspect.a`.
- Archive SHA-256: `e32bf92815b72869a4648dcd0d1262e88f7bb3804971040c67d43ea3119bce92`.
- ELF SHA-256: `35dcea08d88c60d30808300989ab44a26bb17fe4906bb513a7989e7a3ddba466`.

## Reused research and implementation

Read and retained `ASPECT_MASKED_TEXTURE_COPY.md`, `ASPECT_MASKED_GPU_TEST_PLAN.md`
and `evidence/aspect-masked-copy-result.json`. Verified all398 previous receipts
and their sizes/hashes before preparation; did not rerun the unchanged24 encoder
tests. Reused the first deterministic fixture exactly, not a constant clear.

New source is `examples/z24s8_stencil_probe/`:

- `main.cpp`: exact SDK allocation/command/submission/readback adapter and exclusive
  per-run diagnostics. No game assets, swapchain/framebuffer, networking, clock
  settings, performance instrumentation or stress loop.
- `ProbeCore.{h,cpp}`: fixed supported configuration, expected packed/source bytes,
  packing/comparison/guards and completion/lifetime gate shared with host tests.
- `TerminalResult.{h,cpp}`: shared terminal-result formatting and fail-closed POSIX
  publication, also compiled into the real executable.
- `CMakeLists.txt`: pinned archive hash, SDK build, source identity, link map,
  deterministic path mapping, headless NRO/NACP without icon or RomFS.

The existing `tests/stencil_z24s8` harness was extended, not replaced. Its
`--probe-only` filter runs the15 new cases without repeating the original24.
No external GPU-harness framework was needed or fetched.

## Bounded workload and expected bytes

Fixed19x11 ordinary block-linear2D Z24S8, one mip/sample/layer, flags0 (no hardware
compression). Rectangle `(3,2,0)+(5,3,1)`. Stencil is byte0/X; depth is bytes1-3.
Initial texel i is little-endian `(((0x123456+i*0x010307)&0xffffff)<<8) |
((i*37+0x11)&0xff)`. The portable implementation constructs bytes explicitly.

Two cycles each run:

1. **Control A:** ordinary full-packed initialization and ordinary full-packed
   readback. Compare all836 logical bytes. A failure stops before experimentation.
2. **B1:** experimental stencil update with each target's original stencil xorFF;
   ordinary full-packed readback and all comparisons.
3. **B2:** immediately follow B1, without reinitialization, with original stencil
   xor5A; ordinary full-packed readback and all comparisons.

Each update changes exactly15 stencil bytes. All627 depth bytes and194 untouched
texels must retain their expected values. Both source patterns vary per texel.
The second cycle resets the full initial texture using the ordinary path, then
repeats A/B1/B2. This is not a claim of repeatable real-device evidence.

Packed row/layer pitch256/2816 bytes;76 logical bytes per row, minimum touched
span2636 bytes. Stencil source row/layer pitch256/768 bytes; accessible source517
bytes. Transfer addresses use256-byte offsets into separate4096-byte allocations.
No source/destination aliases are intentionally created. Bounds/configuration
checks reject any configuration other than this fixed probe contract.

There are6 test submissions and12 CE copies, excluding stock queue setup/cache
maintenance. Each explicit fence wait is at most5,000,000,000ns (representable as
signed32-bit microseconds). Sum of explicit wait budgets is30seconds, **not a
whole-process wall-clock guarantee**: service allocation, filesystem I/O and OS
termination are not covered by that deadline. Actual hardware duration is unknown.

Explicit allocations: three4096-byte input blocks, six4096-byte readback blocks,
one4096-byte unrelated sentinel,98304 command bytes and a minimum-size65536-byte
queue command ring. Image body and each guard region are bounded to1MiB, for a
maximum3MiB guarded image block. Total explicitly sized blocks are at most3350528
bytes; expected65536-byte layout/alignment yields401408 bytes. These totals exclude
libnx/deko/NV-service bookkeeping, transfer memory, process heap and stack overhead;
those are not measured hardware requirements. Initialization failure is a test
failure, not permission to increase allocations or alter GPU settings silently.

## Independent packed readback contract

Use existing `dkCmdBufCopyImageToBuffer`, `source/dk_image.cpp:815-863,914-929`,
without format override and with copy flags0. Its ordinary `BlitCopyEngine` path
(`source/maxwell/gpu_transfer.cpp:28-117`) sees four-byte Z24S8, multiplies origin
and width by4, and at this small width sets **RemapEnable=false**. It therefore
ignores experimental component-selector registers left behind. This is not an
inverse of the experimental stencil-only operation.

The image's initialized, kind-specific GPU address is used by the SDK. Storage
size/alignment come from the actual layout; logical fixture bytes are never
CPU-memcpy'd into block-linear image storage. The CPU touches only image-allocation
guard pages, never the image body or a generic GPU alias of its packed contents.

Byte ordering is corroborated by deko format/TIC definitions and pinned Mesa
`nil_formats.csv:155` plus `u_format_zs.c:651-680,726-755`. NVK's supported W-stencil
format is not this X-stencil format; its exact-format rejection remains relevant.
An ordinary packed round-trip is **not** an independent shader/depth semantic test.
The broader plan's semantic, GOB-boundary,1x1, rejected-adapter-position and extra
format/layout cases remain future work, not requirements silently claimed complete.

## Synchronization, visibility and ownership

- Upload/readback/unrelated blocks: CpuUncached|GpuUncached. Image allocation:
  Image|CpuUncached|GpuCached; CPU accesses guards only. Commands:
  CpuUncached|GpuCached. No CPU cached-readback invalidate-only API is assumed.
- CPU full-system barriers bracket GPU handoff/readback.
- Each command list: Full+L2/image barrier, ordinary initialization or stencil
  operation, Full+L2/image barrier, ordinary readback, Full+L2/image barrier.
  A single flushing fence gates the entire phase; Full provides the GPU dependency
  between its copy and readback. Control completion gates the first experimental
  submission. CE FlushEnable alone is not the visibility proof.
- Submit list, signal that submission's retained fence with flush=true, flush
  queue, finite wait. Require Success **and** no latched queue error before CPU
  reads, comparisons or progression. Error handling can advance the semaphore;
  fence Success alone is insufficient. Queue-error query is latched, not a fresh
  driver query and not proof every possible fault can be diagnosed.
- Transfer-only queue, no shader/depth rendering, no Zcull allocation; no compressed
  image. There is no live rendering/Zcull producer to synchronize in this probe.
- Distinct command slices/readback blocks, all inputs and GPU owners remain alive
  and inputs unchanged through completion. All six phases retain their own output.
- No resource-owning RAII, queue destruction or waitIdle cleanup: stock healthy
  queue destruction waits indefinitely. All handles remain process-owned until
  termination even on success. Timeout/error fail-stop records FAIL when possible,
  does not read/recycle/unmap anything, and calls svcExitProcess. This is **not a
  proven GPU-hang recovery guarantee** or permission to manipulate console state.

Source audit: `logs/z24s8-hardware-preparation/readback-audit.json` and its complete
read-only transcript. Normal cleanup follows verified completion. After uncertain
completion there is no claimed bounded in-process teardown or manual resource free.

## Actual verification and preserved failures

- Model RED: missing required implementation symbols, exit1, retained.
- Initial six normal model tests passed; instrumented CTest startup emitted recursive
  DEADLYSIGNAL and timed out before cases. Same binary/options bounded repeat6/6.
- Initial independent review **failed** terminal persistence: direct final-file
  writes could leave PASS after sync/close failure. That negative review remains.
- Publication RED: missing publication symbols, exit1, retained.
- Final15 normal host tests passed. Instrumented CTest again hit startup DEADLYSIGNAL
  and30s timeout; unchanged binary with identical sanitizer options passed15/15 in
  a bounded direct repeat. Startup cause/repeatability is **not resolved**.
- Nine publication/metadata tests use actual host POSIX files and shared production
  code, with injected open/write/partial-write/sync/close/rename failures, existing
  result refusal, success ordering and truthful pre-submission metadata. Other six
  exercise expected data, wrong-W/depth/untouched mismatches, bounds and lifecycle.
- Host tests do **not** execute SDK calls or the native GPU adapter. POSIX host
  success is not evidence of HOS/SD runtime semantics or power-loss durability.
- First SDK configure failed because this SDK requires explicit NACP OUTPUT;
  corrected using its actual installed CMake contract. Failure retained.
- Candidate, corrected candidate and two committed-export native builds passed
  with Wall/Wextra/Werror. Actual specified prototype archive linked; no stubs or
  installed deko archive substitution. Static ELF is AArch64 PIE, no interpreter,
  dynamic NEEDED libraries or strong undefined symbols.
- Both committed exports produced **byte-identical ELF, NRO and NACP**. Full NRO
  regeneration from the verified ELF/NACP matches; a changed executable byte is
  rejected. NRO segments, ASET/NACP, title/version, linked symbol providers and
  embedded source/library identities were checked. Target code was never executed.
- C++ host-exported expected/source data matches previous fixture and a separately
  constructed Python reference for both patterns, preserving all depth/outside bytes.
- Optional source-bundle attempt using literal-ID range failed with empty-bundle
  error; preserved. Correct named-HEAD/prerequisite bundle verified successfully.
- Follow-up independent review passed with no material unresolved issue. It did
  not approve GPU behavior, SD durability, hang recovery or hardware deployment.

Reproduce offline with `logs/z24s8-hardware-preparation/prepare.py`, recorded exact
command JSON/build logs, committed-source tar, incremental verified Git bundle
(requires initial d1932746...), and pinned-image/SDK receipts. Use fresh unique
output labels; never overwrite old builds. All acceptance data and raw file
receipts are in `evidence/z24s8-gpu-preparation-result.json`.

## Output protocol and approval package

If separately approved, propose a **new** SD executable filename
`sdmc:/switch/z24s8-stencil-probe-26bf5fa.nro`; refuse to overwrite any existing file.
Headless execution returns to its loader on normal completion; no framebuffer UI.
Each execution exclusively creates a new
`sdmc:/switch/z24s8-stencil-probe-results/run-<system-tick>/` directory. Existing
runs are never reused. Files include progress/identity/allocation/comparison log,
three expected packed images, two stencil sources, three original input-allocation
snapshots, and for each step actual packed bytes, full readback allocation, image
prefix/suffix guards and command-slice bytes (including unused tail, not the whole
SDK-owned queue stream). Final report is `result.json`.

The terminal publisher writes exclusive `result.pending`, completes writes, fsync
and close, checks final absence and then renames as its last fallible operation.
**Only result.json is terminal.** Missing, malformed, FAIL, or pending-only results
are failure/incomplete. Release-library fatal errors bypass cbDebug
(`source/dk_device.h:6-12`, `source/dk_device.cpp:242-247`), so even a FAIL report is
not guaranteed. Missing terminal evidence is never success.

Accept a future run only if all raw files agree with independent expected data,
all guards/immutable inputs match, no I/O/GPU/submission errors are reported, and
terminal JSON has schema_version2, statusPASS, completed_steps=required_steps=
verified_submissions=6, execution_stage=verified_probe_completion and
probe_submission_attempted=true. Any stale/missing output, crash, hang, timeout,
wrong byte/depth/untouched region, guard change or incomplete run fails. One such
passing launch would be one real-device observation, not repeatable hardware proof.

Request only **one deployment under the new filename, one launch, and read-only
retrieval of that execution's new output folder** on the physical Switch1. No
automatic retry, clocks/firmware changes, existing console file modification or
other GPU workload. This document is not that approval. Record console/HOS/loader
conditions only after separately authorized contact; they are currently unknown.

Safe cleanup: do nothing now. After a future authorized run, first establish the
process stopped and archive/hash all raw outputs. Do not free resources manually
in a timed-out live process or claim a safe recovery. If the console is unresponsive,
stop and ask the user; no automatic reboot/recovery is authorized. Remove only the
new exact NRO and that run directory after separate cleanup permission and archival;
never touch older runs, SD contents, benchmarks, games or saves.
