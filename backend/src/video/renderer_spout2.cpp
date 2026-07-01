#include "renderer.h"

#include <SpoutDX/SpoutUtils.h>

void CRenderer::Init_SPOUT2() {
    OpenSpoutConsole(); // Console only for debugging
    // Enable this to see error logs to trace problems
    EnableSpoutLog(); // Log to console
    sender_SPOUT2.OpenDirectX11();
    sender_SPOUT2.CreateSender("dejavis-ui Sender",core_w,core_h);
    spout2_sender_active = true;
}

void CRenderer::Close_SPOUT2() {
    spout2_sender_active = false;
    sender_SPOUT2.ReleaseVulkanImage(m_ctx.device);
    sender_SPOUT2.ReleaseSender();
    sender_SPOUT2.ReleaseSharedDX11texture();
    sender_SPOUT2.CloseDirectX11();
}