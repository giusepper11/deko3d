# Proposed future GM20B stencil-preservation test — NOT AUTHORIZED TO RUN

This is a test plan and host-generated reference data, **not an executed GPU test**.
Obtain explicit user approval before building/deploying a runnable console probe,
contacting hardware, writing console/SD files or executing this experiment.
Do not use a game, production save, existing console file or previous benchmark
artifact. Keep a new isolated test output and retain failures/raw readbacks.

## Preparation mission update — still no deployment authorization

The subsequent user mission explicitly authorized preparation and building only.
That approval produced the minimal probe recorded in
[Z24S8_GPU_PREPARATION.md](Z24S8_GPU_PREPARATION.md), implementation
`26bf5fa842d2a498b675fa53ed3455ee1ea1deef`, with a reproducible exact NRO and a
fresh hardware-approval package. No console was contacted or test executed.
Read that record for actual readback/barrier/completion contracts, two consecutive
variable updates per cycle, diagnostics, failed results and the precise approval
gate. The broader semantic, GOB/1x1, format and adapter-rejection cases below remain
future work; preparation does not claim them complete or weaken their criteria.

## Narrow question

On Switch1 Maxwell GM20B, does the experimental single-layer uncompressed Z24S8
buffer→image command sequence with remap0x03006660 and ForceRmwDisable=false change
only stencil byte0 while preserving all existing depth bytes1..3?

## Ready host fixtures

`builds/deko3d-aspect-copy/future-gpu-fixture/fixture.json` records the generated
inputs, expected logical texels and SHA256 hashes. The reproducible host-only
producer is `logs/deko3d-aspect-copy/prepare_gpu_fixture.py`.

- Image: ordinary2D Z24S8,19×11, one mip, one sample, no HW compression.
- Rectangle: (3,2,0)+(5,3,1); source one byte per stencil texel.
- Row pitch256; image pitch768 bytes; accessible source region517 bytes.
- Destination padded width32 texels, tileH1, existing Z24S8 memory-kind mapping.
- Initial836-byte logical packed image has distinct depth/stencil patterns.
- Expected836-byte logical result changes exactly15 stencil bytes and no depth
  byte. Padding in the source is a sentinel and must not affect destination data.

These buffers are logical row-major reference data, **not block-linear storage
images**. Use supported image-copy operations to initialize/read back the GPU
image; do not memcpy these logical bytes directly into block-linear image memory.

## Procedure to review and approve before execution

1. Record exact source/library/probe/compiler identities and GM20B device details.
   Use an isolated minimal probe, not a full Dusklight NRO.
2. Allocate a new block-linear uncompressed Z24S8 image with a valid image-capable
   allocation/memory-kind mapping. Allocate separate upload/readback/command
   memory, meeting the documented alignment and lifetime contract. Confirm
   resources do not overlap or alias through distinct virtual mappings.
3. Upload the complete known packed initial contents using the existing ordinary
   four-byte copy path. Perform the appropriate visibility/order operations,
   complete a fence, read back and compare all836 logical bytes to the initial
   fixture. If this control fails, stop; do not attribute later mismatches to
   stencil remapping or adjust expected depth values.
4. Prepare the517-byte stencil source in a256B-aligned GPU buffer. Choose and
   record CPU/GPU cache modes and apply the required CPU cache maintenance.
   Fill allocation guards/surrounding logical texels with known sentinels.
5. Review the actual pre-copy ordering: preceding render/compute/transfer writes
   must be complete and visible to CE read/modify/write. Existing full barriers,
   L2/image cache operations and Zcull invalidation where appropriate are
   separate from the copy. Do not treat FlushEnable as a universal dependency.
6. Record one experimental upload; capture/decode its actual command stream and
   compare with the host fixture. Confirm source/destination component counts,
   X selection, Y/Z/W NoWrite, element-unit origins, byte pitch, line width/count,
   block size/memory kind, ForceRmwDisable=false and setup-before-launch order.
7. Review post-copy visibility, invalidate the relevant consumers/caches, fence
   completion and perform ordinary image→buffer readback into a separate buffer.
   Invalidate CPU cache if required before reading. Keep all resources and source
   bytes alive/unmodified until completion.
8. Compare the entire logical result against expected-packed.bin. Report updated
   stencil values, exact byte equality of depth bytes, untouched texels/guards,
   synchronization settings and raw output hashes separately. A mismatch is a
   failure, not permission to reinterpret byte order or expected values silently.
9. Repeat with several changing stencil patterns and with repeated submissions,
   including identical repeated values. Include fresh1×1 controls and subsequent
   GOB-boundary cases spanning x15/16 and y7/8, and nonzero XY origins. Separate
   each tested tileH/configuration; do not generalize untested layouts.
10. Add an independent semantic depth/stencil check, e.g. correctly configured
    depth/stencil sampling or comparison, to confirm the packed-byte interpretation
    rather than relying solely on round-trip raw copies. Review that setup first.
11. Test ordinary copy → aspect copy → ordinary copy sequencing and rejected-input
    command-position preservation in the real adapter. Record negative cases
    without submitting deliberately invalid GPU packets.

## Pass/fail gate

Pass only for individually tested configurations if the initial control is exact,
new stencil values are exact, **all depth bytes are bit-identical**, untouched
regions/guards are unchanged, repeated operations remain correct, and visibility/
completion conditions are explicit. Preserve every failure and original readback.
A crash, hang, timeout, stale read, unexplained mismatch or cache-dependent result
blocks broader acceptance. No FPS or performance conclusion follows from this test.

Even a future passing test would not establish support for compressed images,
MSAA,3D/arrays/mips, S8Z24, ZF32_X24S8, depth-only transfers, overlapping resources,
or an upstream API contract. Those need separate design, evidence and approval.
