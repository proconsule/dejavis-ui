#ifndef DEJAVIS_APP_YUV_CONVERTER_H
#define DEJAVIS_APP_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <memory>
#include <cstdint>

#include "../render_globals.h"
#include <Processing.NDI.Lib.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// Forward
struct YUV2RGBSlotResources;

// =============================================================================
//  IConverterSlot — risorse interne di uno slot, opache al manager.
//  Ogni converter definisce la SUA sottoclasse di questa interfaccia.
// =============================================================================
class IConverterSlot {
public:
    virtual ~IConverterSlot() = default;
};

// =============================================================================
//  IYUVConverter — pipeline di conversione specializzata per un singolo
//  formato di input (o per una famiglia di formati molto simili).
// =============================================================================
class IYUVConverter {
public:
    virtual ~IYUVConverter() = default;

    virtual const char* name() const = 0;
    virtual bool        supports(int avPixFmt) const = 0;

    virtual bool init(VulkanContext* ctx) = 0;
    virtual void shutdown() = 0;

    struct OutputBinding {
        VkImage     image;
        VkImageView view;
        uint32_t    width;
        uint32_t    height;
    };

    virtual std::unique_ptr<IConverterSlot>
        createSlot(uint32_t width, uint32_t height,
                   const OutputBinding& out) = 0;

    virtual void destroySlot(IConverterSlot* slot) = 0;

    virtual bool uploadFrame(IConverterSlot* slot, AVFrame* f) = 0;
    virtual bool uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) = 0;


    struct FrameMetadata {
        int colorSpace;
        int rangeFull;
    };

    virtual void recordDispatch(VkCommandBuffer cmd,
                                IConverterSlot* slot,
                                const FrameMetadata& meta,
                                VkImageLayout& outCurrentLayout) = 0;
};

#endif // DEJAVIS_APP_YUV_CONVERTER_H