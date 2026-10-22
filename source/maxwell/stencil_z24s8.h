// Original, local experimental encoder. Project LICENSE applies; not upstream code.
// Reference mechanism: Mesa/NVK nvk_remap_insert_aspect and nouveau_copy_rect.
// deko Z24S8 matches PIPE_FORMAT_S8_UINT_Z24_UNORM, NOT Z24_UNORM_S8_UINT.
// Pinned NVK rejects S8_UINT_Z24_UNORM (nvk_cmd_copy.c:490-493). The X mapping
// follows independent format/register contracts, not NVK execution for this format.
#pragma once
#include <deko3d.h>
#include "engine_copy.h"
#include "helpers.h"

namespace dk::detail
{
struct StencilZ24S8Image
{
    DkImageType type;
    DkImageFormat format;
    uint32_t flags, width, height;
    uint32_t mipLevels, samplesLog2, tileW, tileH, tileD;
    bool uncompressedZ24S8Kind;
    DkGpuAddr address;
    uint64_t storageSize;
};

struct StencilZ24S8Copy
{
    DkGpuAddr source, destination;
    uint32_t rowPitch, sourceImagePitch, destinationWidth, destinationHeight;
    uint32_t tileH, x, y, width, height;
    uint64_t requiredSourceBytes;
};

inline DkResult PrepareStencilZ24S8Copy(StencilZ24S8Image const& image,
    DkCopyBuf const* src, uint64_t srcSize, DkImageView const* view,
    DkImageRect const* rect, StencilZ24S8Copy& out)
{
    if (!src || !view || !view->pImage || !rect)
        return DkResult_BadInput;
    if (image.format != DkImageFormat_Z24S8 ||
        (view->format != DkImageFormat_None && view->format != image.format) ||
        image.type != DkImageType_2D ||
        (view->type != DkImageType_None && view->type != DkImageType_2D) ||
        image.mipLevels != 1 || image.samplesLog2 || view->mipLevelOffset ||
        view->mipLevelCount > 1 || view->layerOffset || view->layerCount > 1 ||
        (image.flags & (DkImageFlags_PitchLinear | DkImageFlags_HwCompression)) ||
        !image.uncompressedZ24S8Kind || image.tileW || image.tileD || image.tileH > 5)
        return DkResult_NotImplemented;
    // Deliberately conservative prototype limits, not general hardware limits.
    if (!image.width || !image.height || image.width > 16384 || image.height > 16384 ||
        !rect->width || !rect->height || rect->z || rect->depth != 1 ||
        rect->x > image.width || rect->width > image.width - rect->x ||
        rect->y > image.height || rect->height > image.height - rect->y ||
        !src->rowLength || src->rowLength < rect->width)
        return DkResult_BadInput;
    if (src->rowLength % 256)
        return DkResult_MisalignedSize;
    const uint64_t imagePitch = uint64_t(src->rowLength) * rect->height;
    const uint64_t required = uint64_t(src->rowLength) * (rect->height - 1) + rect->width;
    if (imagePitch > UINT32_MAX || (src->imageHeight && src->imageHeight != imagePitch) ||
        required > srcSize)
        return DkResult_BadInput;
    constexpr uint64_t addressLimit = uint64_t{1} << 40;
    if (!src->addr || src->addr >= addressLimit || srcSize > addressLimit - src->addr ||
        !image.address || image.address >= addressLimit ||
        image.storageSize > addressLimit - image.address)
        return DkResult_BadInput;
    if (src->addr % 256 || image.address % 512)
        return DkResult_MisalignedData;
    const uint32_t alignedWidth = (image.width + 15) & ~15U;
    const uint32_t tileHeight = 8U << image.tileH;
    const uint32_t alignedHeight = (image.height + tileHeight - 1) & ~(tileHeight - 1);
    if (uint64_t(alignedWidth) * 4 * alignedHeight > image.storageSize)
        return DkResult_BadInput;
    // The image's full allocation is conservatively excluded from the source span.
    // Address-space aliasing through different GPU virtual addresses is a caller precondition.
    if (src->addr < image.address + image.storageSize && image.address < src->addr + srcSize)
        return DkResult_BadInput;
    out = {src->addr, image.address, src->rowLength, uint32_t(imagePitch),
           alignedWidth, image.height, image.tileH, rect->x, rect->y,
           rect->width, rect->height, required};
    return DkResult_Success;
}

inline auto EncodeStencilZ24S8Copy(StencilZ24S8Copy const& copy)
{
    using namespace maxwell;
    using E = EngineCopy;
    using R = E::SetRemapComponents;
    using L = E::LaunchDma;
    // Numeric NoWrite (6) is documented in the pinned engine_copy.def. Source X
    // maps to destination byte0 (stencil); depth bytes1..3 are not written.
    constexpr uint32_t remap = R::DstX{0} | R::DstY{6} | R::DstZ{6} | R::DstW{6} |
        R::ComponentSize::One | R::NumSrcComponents::One | R::NumDstComponents::Four;
    constexpr uint32_t launch = L::TransferType::NonPipelined | L::FlushEnable{} |
        L::SrcMemoryLayout::Pitch | L::DstMemoryLayout::BlockLinear |
        L::MultiLineEnable{} | L::RemapEnable{};
    // FORCE_RMW_DISABLE deliberately remains zero. NoWrite hardware behavior
    // still needs explicit GM20B testing; command encoding is not that test.
    return Cmds(
        Cmd(Copy, SetRemapComponents{}, remap),
        Cmd(Copy, SetDstBlockSize{}, 0x1000U | (copy.tileH << 4),
            copy.destinationWidth, copy.destinationHeight, 1U, 0U, copy.x | (copy.y << 16)),
        Cmd(Copy, OffsetIn{}, Iova(copy.source), Iova(copy.destination),
            copy.rowPitch, 0U, copy.width, copy.height),
        CmdInline(Copy, LaunchDma{}, launch));
}
}
