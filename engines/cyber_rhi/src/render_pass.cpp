// Structural render pass validation.
//
// The checks here need nothing but the description: presence, duplicates, load
// and store combinations, the read-only depth rules, and clear values. Anything
// that needs to look at an attachment's texture — its extent, its format, its
// sample count, its tracked state — belongs to the backend, which is the only
// place that can resolve a view.
//
// Splitting it this way means a renderer can validate a pass before it is
// recorded, and a test can exercise every rule without a device.

#include <cyber/rhi/render_pass.h>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;

[[nodiscard]] Status<Error> Fail(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

}  // namespace

Status<Error> ValidateRenderPassDesc(const RenderPassDesc& desc) noexcept
{
    if (desc.colorAttachments.Empty() && !desc.hasDepthStencil)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a render pass needs a color attachment or a depth attachment");
    }
    if (desc.colorAttachments.Size() > kMaxColorAttachments)
    {
        return Fail(ErrorCode::OutOfRange,
                    "a render pass has more color attachments than kMaxColorAttachments");
    }
    // A depth attachment supplied without the flag is a caller mistake that
    // would otherwise silently drop the depth buffer from the pass.
    if (!desc.hasDepthStencil && desc.depthStencil.view.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a render pass has a depth view but hasDepthStencil is false");
    }

    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachment& attachment = desc.colorAttachments[index];
        if (!attachment.view.IsValid())
        {
            return Fail(ErrorCode::InvalidArgument, "a render pass color attachment has no view");
        }
        if (!IsValidLoadOp(attachment.loadOp))
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a render pass color attachment has an invalid load operation");
        }
        if (!IsValidStoreOp(attachment.storeOp))
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a render pass color attachment has an invalid store operation");
        }
        if (attachment.loadOp == LoadOp::Clear && !attachment.clearValue.IsFinite())
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a render pass clear value has a NaN or infinite component");
        }
        if (attachment.resolveView.IsValid())
        {
            if (attachment.resolveView == attachment.view)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a render pass attachment cannot resolve into itself");
            }
            if (attachment.storeOp != StoreOp::Store)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a resolving attachment must store its contents");
            }
        }
        // The same view twice is an aliasing hazard no backend can honour: the
        // first write and the second write would race inside one pass.
        for (usize other = 0; other < index; ++other)
        {
            if (desc.colorAttachments[other].view == attachment.view)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a render pass binds the same color view twice");
            }
            if (desc.colorAttachments[other].resolveView == attachment.view ||
                desc.colorAttachments[other].view == attachment.resolveView)
            {
                return Fail(ErrorCode::InvalidArgument,
                            "a render pass uses one view as an attachment and a resolve target");
            }
        }
    }

    if (!desc.hasDepthStencil)
    {
        return Status<Error>::Ok();
    }

    const DepthStencilAttachment& depth = desc.depthStencil;
    if (!depth.view.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "a render pass depth attachment has no view");
    }
    if (!IsValidLoadOp(depth.depthLoadOp) || !IsValidLoadOp(depth.stencilLoadOp))
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a render pass depth attachment has an invalid load operation");
    }
    if (!IsValidStoreOp(depth.depthStoreOp) || !IsValidStoreOp(depth.stencilStoreOp))
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a render pass depth attachment has an invalid store operation");
    }
    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        if (desc.colorAttachments[index].view == depth.view ||
            desc.colorAttachments[index].resolveView == depth.view)
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a render pass uses one view as a color and a depth attachment");
        }
    }

    if (depth.readOnly)
    {
        // A read-only attachment is tested against, never written. Clearing is a
        // write, and discarding the result would break the next pass that reads
        // what this one deliberately preserved.
        if (depth.depthLoadOp == LoadOp::Clear)
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a read-only depth attachment cannot be cleared");
        }
        if (depth.depthStoreOp != StoreOp::Store)
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a read-only depth attachment must store its contents");
        }
    }
    if (depth.depthLoadOp == LoadOp::Clear || depth.stencilLoadOp == LoadOp::Clear)
    {
        if (!depth.clearValue.IsValid())
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a render pass depth clear value is outside 0..1 or not finite");
        }
    }
    return Status<Error>::Ok();
}

}  // namespace cyber::rhi
