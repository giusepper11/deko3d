/* Local research extension; not part of upstream deko3d or a finalized public API.
 * Original implementation; project LICENSE applies. No NVK source text copied.
 */
#pragma once
#include <deko3d.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record a stencil-only DMA copy into an uncompressed DkImageFormat_Z24S8 image.
 * EXPERIMENTAL: command encoding/model tested only; GM20B hardware unverified.
 * Contract: ordinary block-linear 2D, single sample, single mip, no layers,
 * no format/type reinterpretation or flips. Rectangle z=0 and depth=1.
 * Source is one stencil byte per texel; addr is already offset and 256B-aligned;
 * rowLength is an explicit positive multiple of 256 bytes and >= rectangle width.
 * imageHeight is 0 or rowLength*rectangle height (bytes, not source row count).
 * srcSize is the accessible byte count beginning at src->addr; no unused final-row
 * padding is required. Destination base is 512B-aligned. GPU ranges are <2^40.
 * Unsupported layouts return NotImplemented; malformed inputs return BadInput or
 * alignment errors, in release as well as debug. No commands emitted on validation
 * failure. Existing command-memory exhaustion behavior is unchanged.
 * Caller owns valid non-overlapping GPU resources, cache maintenance, inter-engine
 * barriers and lifetime through completion. This records no queue/fence/barrier,
 * allocates no upload storage and takes no resource ownership. No hardware use
 * is authorized by the existence of this experimental function.
 */
DkResult dkCmdBufCopyBufferToImageStencilZ24S8Experimental(
    DkCmdBuf obj, DkCopyBuf const* src, uint64_t srcSize,
    DkImageView const* dstView, DkImageRect const* dstRect);

#ifdef __cplusplus
}
#endif
