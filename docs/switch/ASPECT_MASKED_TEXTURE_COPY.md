# Aspect-masked Z24S8 stencil copies — controlled deko3d prototype

## Outcome and boundary

**RESEARCH PASS; PROTOTYPE HOST/BUILD PASS, within the contract below.**
This is source, host-model/command-encoding, ABI and static-library evidence only.
There is **no GM20B hardware result**, finalized public API, production acceptance,
performance claim, or Dawn integration approval. The previous Dawn texture-upload
candidate remains unaccepted and unchanged.

All work is isolated in `dependencies/deko3d-aspect-copy`, branch
`research/stencil-z24s8-remap`. Research records follow the existing `docs/switch/`
convention here rather than modifying a protected Dusklight checkout.

- deko3d base: `373bfd43d8b2d805fa2eb7d406dbb823032df958`.
- Local prototype implementation: `cf53748dd02f007758e1518d8c49610eef117671`.
- Mesa/NVK reference: `0ff52079ca9e9e837ed27eb95a8d239f439a1864`.
- Standalone dekotools source: `c15523cea963c04fad77a46f23c3a96d26a86ab9`.
- Evidence-only commit is separate from the implementation commit.

## Mechanism and the important format distinction

NVK `src/nouveau/vulkan/nvk_cmd_copy.c` constructs separate source/destination
layouts, selects a component remap, then emits DMA Copy Engine commands:

- `nvk_remap_insert_aspect`:444.
- `nvk_cmd_copy_buffer_to_image_ce`:501.
- `nouveau_copy_rect`:203–420.
- `nvk_remap_extract_aspect`:570 (reference only; no extraction prototype).

The Maxwell DMA_COPY_A class is **0xB0B5**, also selected by the Horizon GM20B
setup. Its official class header and deko3d `engine_copy.def` agree on component
selectors, size/count fields, multiline remapping, packed origins and launch bits.
No Pascal-or-later requirement appears for the remapping used here.

**Do not equate names across the two projects.**

| Source format | Little-endian bytes0..3 | Stencil insertion selectors X/Y/Z/W |
|---|---|---|
| NVK `PIPE_FORMAT_Z24_UNORM_S8_UINT` | depth low/mid/high, stencil | NoWrite/NoWrite/NoWrite/SrcX |
| deko `DkImageFormat_Z24S8`, source-correlated to `PIPE_FORMAT_S8_UINT_Z24_UNORM` | stencil, depth low/mid/high | SrcX/NoWrite/NoWrite/NoWrite |

Evidence for deko's stencil byte0/X:

- `source/maxwell/format_traits.inc:44`: four-byte texel, TIC `Z24S8` and depth
  target named `S8Z24Unorm` by deko.
- `source/maxwell/image_formats.h:49–51,175–184`: numeric TIC0x29 and Z-target0x14.
  The deko symbolic Z-target name is misleading across implementations; compare
  numeric values, not spelling.
- Mesa `src/nouveau/nil/nil_formats.csv:149–161` maps the matching target/TIC pair
  to `S8_UINT_Z24_UNORM`.
- Mesa `src/util/format/u_format.yaml:617–628` and
  `u_format_tests.c:397–405` distinguish `[UN24,UP8]` from `[UP8,UN24]`.
- Mesa `src/nouveau/nil/image.rs:843–859` maps `S8_UINT_Z24_UNORM` to the same
  uncompressed Z24S8 memory kind as deko `image_formats.cpp:59–68`.
- deko `tic_generate.cpp:49–55` selects the R/X component for stencil sampling.

**Pinned NVK explicitly rejects `PIPE_FORMAT_S8_UINT_Z24_UNORM`** in the insertion
switch at490–493. Therefore NVK is evidence for the shared command mechanism, not
an execution precedent for deko's exact byte order. The proposed X mapping is
independently supported by the format/register tables. Physical preservation
under that mapping remains untested.

### Exact proposed remapping

One byte per component, **one source component**, **four destination components**:

```
DstX = SrcX (0)
DstY = NoWrite (6)
DstZ = NoWrite (6)
DstW = NoWrite (6)
ComponentSize = One (encoded 0)
NumSrcComponents = One (encoded 0)
NumDstComponents = Four (encoded 3)
SET_REMAP_COMPONENTS = 0x03006660
```

`NoWrite` is selector6, not zero-fill and not a constant selector. Source headers
name it NO_WRITE and NVK uses it to preserve unselected destination components;
this is the requested hardware operation, not proof of its execution here.
NVK's W-byte mapping would instead be0x03000666 and would select a depth byte in
deko's layout. Tests explicitly reject that counterfactual.

`FORCE_RMW_DISABLE` remains clear. The launch is **0x686**:
NON_PIPELINED, FLUSH_ENABLE, source PITCH, destination BLOCKLINEAR, MULTI_LINE and
REMAP_ENABLE. NON_PIPELINED follows existing deko3d behavior; NVK buffer uploads
use PIPELINED. FlushEnable alone does not supply inter-engine barriers, CPU cache
maintenance, completion or resource lifetime.

## Addressing, strides and command stream

Existing deko `dk_image.cpp:697–745` assumes the source block size equals the
complete destination format. The ordinary `BlitCopyEngine`,
`source/maxwell/gpu_transfer.cpp:28–116`, uses remapping for some larger layouts,
but with identity mapping. It is not an aspect-selective upload contract.

`ImageInfo::fromImageView`, `dk_image.cpp:180–248`, already describes mip/layer
bases, tiling and aligned block-linear horizontal sizes. The prototype deliberately
handles only mip0 of an ordinary single-layer2D image, so no mip/layer rebasing
is needed. It independently derives the same padded width for four-byte texels:
`alignUp(imageWidth,16)` texels, corresponding to64-byte GOB width. The destination
base remains the existing image GPU address/memory-kind mapping.

With one-byte remap components, CE `SET_DST_WIDTH`, destination X origin and
`LINE_LENGTH_IN` use remapped element/texel units, **not width×4 or x×4**. Source
row pitch remains bytes. Destination height is the complete logical image height;
block height follows image tileH; depth=1 and layer=0. The hardware, not this host
model, performs the block-linear address transformation.

One accepted upload emits exactly19 command words, using the real deko command
helpers and the real generated engine header:

1. `SET_REMAP_COMPONENTS` at method-word0x1c2 (byte address0x708).
2. Destination block size, full padded width/height, depth, layer and packed XY
   origin at0x1c3–0x1c8.
3. Source/destination high/low addresses, PITCH_IN, zero PITCH_OUT,
   LINE_LENGTH_IN and LINE_COUNT at0x100–0x107.
4. Inline LAUNCH_DMA at0x0c0, **after all setup**.

The real emitted fixture uses a19×11 destination, tileH=1, copy rectangle
(3,2,0)+(5,3,1), source pitch256 and517 required bytes. It decodes to destination
width32, height11, originX3/Y2, line length5/count3 and image pitch768. Image pitch
is validated/normalized but is not a separate CE register or layer advancement
for this one-layer contract. No unused final source-row padding is required.

Every invocation programs its own remapping. Existing ordinary copies disable
remapping or replace its fields; they were not edited. Runtime alternation with
ordinary copies remains a future adapter/hardware test, not a current host claim.

## API alternatives and chosen experimental contract

| Concern | A — extend existing copy with aspect selection | B — separate aspect-aware operation |
|---|---|---|
| Compatibility | A new flag can preserve default calls, but changes existing validation/semantics | Existing declaration, implementation and defaults stay byte-identical |
| Implementation | Needs a dispatch branch plus distinct source-layout handling | Small separate validated adapter and encoder |
| Validation/formats | Must distinguish ordinary flags, aspects and format support | Can explicitly limit this experiment to one format/layout |
| Consumer complexity | Fewer entry points, but context-dependent source block size | Explicit call and result handling; no silent destructive fallback |
| Reusability | Potentially general after hardware/format validation | Reusable outside Dusklight without exposing raw registers |
| Ownership/synchronization | Caller responsibility remains | Same; no new owner, queue or allocator |

**Recommend B for the controlled experiment.** A separate header declares
`dkCmdBufCopyBufferToImageStencilZ24S8Experimental`. This deliberately format-named
entry point does not finalize a general upstream API. Broader aspect enums,
additional formats or integration must wait for evidence. No arbitrary register
controls are public.

Implementation files:

- `include/deko3d_aspect_copy.h`: experimental C-compatible declaration/contract.
- `source/dk_image_stencil.cpp`: validates pointers, describes the existing image,
  calls the shared planner, and reserves/appends commands through CmdBufWriter.
- `source/maxwell/stencil_z24s8.h`: shared validated planner and19-word encoder.
- `tests/stencil_z24s8/`: focused host model/encoding/invalid-input tests.

Supported by the prototype contract only:

- Buffer→image, stencil-only, actual backing format Z24S8 and its uncompressed
  Z24S8 memory kind; no format reinterpretation.
- Block-linear ordinary2D, one sample, one mip, one layer; no flips or3D.
- TileW=tileD=0; tileH0..5; conservative width/height limits1..16384.
- Source is one stencil byte per texel. GPU address is already offset and256B
  aligned; explicit row pitch is a positive256-byte multiple and at least width.
- `DkCopyBuf.imageHeight` is zero or rowPitch×copyHeight **bytes**, not rows.
- Source span must cover `(height-1)*rowPitch+width`; image pitch must fit uint32.
- Destination base512B aligned; declared source and destination ranges below2^40,
  with no overlap. Different-VA aliasing remains forbidden by caller precondition.
- Rectangle z=0/depth=1 and subtraction-checked XY bounds.

These alignment/extent restrictions are deliberately conservative experiment
limits, **not a claim about every layout the hardware could support**. Malformed
inputs return BadInput/alignment errors; unsupported layouts return NotImplemented
in release and debug. Validation returns before command emission. The existing
command-memory allocation/exhaustion policy is unchanged.

Unsupported: depth-only writes, S8Z24 ordering, Z24X8, Z16, standalone S8,
ZF32_X24S8, color/BC formats, MSAA, hardware compression, mip chains, arrays,
cubemaps,3D, pitch-linear destination, flips, image→buffer and image→image.
No other format is inferred safe from this result.

## Executed verification and limits

Final reviewed and fresh tracked-only committed-source exports each pass:

- **24/24 normal host cases**.
- **24/24 ASan+UBSan cases**, leak detection and halt-on-error enabled.
- Full release AArch64 static-library build (all27 source objects).
- C11/C++17 experimental function declaration compatibility.
- Original ABI probe values match: `[8,4,8,16,8,48,8,24,128,8,128,8]`
  for pointer/result/address, CopyBuf size/alignment, ImageView size/alignment,
  ImageRect size, Image size/alignment and ImageLayout size/alignment.
- All original26 object files are byte-identical after debug stripping against a
  fresh baseline build using the same toolchain. No original strong symbol is
  missing; the only new strong symbol is the experimental function.
- Existing `include/deko3d.h`, `dk_image.cpp` and `gpu_transfer.cpp` are unchanged.
- Actual emitted binary fixture decoded against literal Maxwell register values
  and packet ordering. The test uses real CmdList encoding, not an invented SDK
  response or a mocked register packer.

The reference model initializes distinct depth/stencil bytes, applies varying
stencil values repeatedly, and confirms depth bytes and untouched texels remain
identical **in that model**. It does not emulate hardware swizzling, RMW or caches.
Host tests exercise the exact shared planner/encoder, not a host execution of the
real entry-point adapter/private CmdBuf interaction. Full AArch64 compilation
covers the adapter's real types and implementation but cannot supply runtime
behavior. Broader acceptance requires rejected-input command-position and ordinary
copy alternation tests too.

One independent read-only NVK analyst corroborated the format distinction. One
separate read-only reviewer returned passed:true with no material source-level
finding within this contract. Its documentation/branch-coverage suggestions were
implemented; its adapter/runtime suggestion remains an explicit broader-acceptance
limitation. No follow-up hardware approval is implied.

### Preserved negative evidence

No failed run was erased or converted into success:

- Initial host setup could not locate dekodef. Pinned existing dekotools were then
  built into a fresh isolated output; no source/toolchain installation was changed.
- Initial inner-Make invocation omitted exported object variables and produced an
  empty8-byte archive. It was detected/rejected and never accepted or linked.
- Subsequent inner-Make attempts omitted generated-header dependency variables;
  missing generated headers were preserved. Correct fresh invocations supply the
  upstream inner-build variables; no cleanup/install targets run.
- One prototype include path worked for the host target but not upstream's build;
  it was corrected to a sibling-relative helpers.h include before final tests.
- One verification helper initially tried to create an output directory already
  created by its container helper; corrected proof uses a new subdirectory.
- A19-case sanitizer repeat recursively emitted DEADLYSIGNAL before case output
  and hit CTest's30-second timeout. That235MB raw log and CTest files remain.
  The unchanged binary later passed19/19 with original options; both final24-case
  suites passed. Sanitizer startup repeatability is **not** established.

The detailed commands, raw logs, fixture, compiler/version results and archive/
object/source hashes are indexed by `evidence/aspect-masked-copy-result.json`.
The final committed-source archive is3,478,728 bytes, SHA256
`e32bf92815b72869a4648dcd0d1262e88f7bb3804971040c67d43ea3119bce92`, at
`builds/deko3d-aspect-copy/cross-committed/libdeko3d-aspect.a`.
DevkitA64 GCC15.2.0, libnx4.12.0-1 and toolchain image
`sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c`
were used. No installed deko3d archive was replaced.

## Ownership, synchronization and future hardware gate

The API records commands only. It allocates no uploads, retains no caller memory,
submits no queue, emits no barrier/fence and changes no memory manager. Both GPU
allocations and command memory must remain valid until completion; source bytes
must not change while in flight. Existing producers must finish and make their
writes visible before the DMA read/partial destination write. Consumers must see
DMA output before sampling, rendering, depth/stencil use or readback.

Relevant existing mechanisms are CPU-cache maintenance, `dkCmdBufBarrier`, full
ordering, image/L2 invalidation, Zcull invalidation where needed, and completion
fences. `gpu_base.cpp:49–116` implements these separately from the copy. No claim
is made that FlushEnable or NonPipelined alone satisfies all of them. Aliased or
concurrent writes are not supported. Compression, sparse/other tiling and semantic
depth interpretation must not be generalized from byte-model tests.

The next narrow GPU experiment is specified in
[ASPECT_MASKED_GPU_TEST_PLAN.md](ASPECT_MASKED_GPU_TEST_PLAN.md).
**Explicit approval is required before any execution or console contact.**

## Provenance and upstream recommendation

NVK reference file: MIT, copyright2022 Collabora Ltd. and Red Hat Inc. Vendor
register headers carry NVIDIA's permissive notice. Deko3d is fincs2018–2020 under
its zlib-style LICENSE. All notices remain intact; new files are explicitly marked
as original local experiments. No NVK source text was copied; the reference informs
format/register semantics. Standalone dekotools are GPLv3 build tools, not linked
into the deko3d library. Their exact source/tool identities are recorded.

A focused upstream **design discussion** is justified by shared Maxwell register
contracts and this bounded encoding proof. A production patch/API proposal is
premature until real GM20B tests establish NoWrite preservation, correct block-linear
origins/GOB boundaries, visibility and repeated operation, and maintainers review
the intended public contract. No contribution was submitted or published.
