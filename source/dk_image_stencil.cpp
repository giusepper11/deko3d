// Original local experiment, not upstream deko3d. Project LICENSE applies.
#include "dk_image.h"
#include "cmdbuf_writer.h"
#include <deko3d_aspect_copy.h>
#include "maxwell/stencil_z24s8.h"

using namespace dk::detail;

DkResult dkCmdBufCopyBufferToImageStencilZ24S8Experimental(DkCmdBuf obj,
    DkCopyBuf const* src, uint64_t srcSize, DkImageView const* dstView,
    DkImageRect const* dstRect)
{
    // Unlike ordinary debug-only deko3d validation, this deliberately narrow
    // research contract is checked in both release and debug builds.
    if (!obj || !dstView || !dstView->pImage)
        return DkResult_BadInput;
    auto const& image = *dstView->pImage;
    StencilZ24S8Image description{
        image.m_type, image.m_format, image.m_flags,
        image.m_dimensions[0], image.m_dimensions[1], image.m_mipLevels,
        image.m_numSamplesLog2, image.m_tileW, image.m_tileH, image.m_tileD,
        image.m_memKind == NvKind_Z24S8, image.m_iova, image.m_storageSize};
    StencilZ24S8Copy copy{};
    DkResult result = PrepareStencilZ24S8Copy(description, src, srcSize, dstView, dstRect, copy);
    if (result != DkResult_Success)
        return result;
    DK_ENTRYPOINT(obj);
    CmdBufWriter writer{obj};
    writer.reserveAdd(EncodeStencilZ24S8Copy(copy));
    return DkResult_Success;
}
