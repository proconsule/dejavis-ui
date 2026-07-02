#include "renderer.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

static float imgui_welcome_timerNotify = 5.0f;
static const char* imgui_welcome_msg = "Welcome to dejavis-ui";

bool CRenderer::InitImGuiDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(m_ctx.device, &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallimento nella creazione del Descriptor Pool per ImGui");
        return false;
    }
    return true;
}

void CRenderer::Init_ImGui() {

    DEJAVISUI_LOG_DEBUG("[UI] Initializing ImGui...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    auto loader = [](const char* name, void* user_data) -> PFN_vkVoidFunction {
        return vkGetInstanceProcAddr((VkInstance)user_data, name);
    };
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, loader, (void*)m_ctx.instance);

    ImGui_ImplGlfw_InitForVulkan(glfw_window, true);


    if (!InitImGuiDescriptorPool()) {
        DEJAVISUI_LOG_ERROR("Init_ImGui: pool creation failed");
        return;
    }

    ImGui_ImplVulkan_InitInfo init_info = {};

    init_info.Instance = m_ctx.instance;
    init_info.PhysicalDevice = m_ctx.physicalDevice;
    init_info.Device = m_ctx.device;
    init_info.QueueFamily = m_ctx.graphicsQueueFamily;
    init_info.Queue = m_ctx.graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = m_display.swapchainImages.size();
    //init_info.Allocator = g_Allocator;
    init_info.PipelineInfoMain.RenderPass = m_master_per_frame[0].renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;


    ImGui_ImplVulkan_Init(&init_info);
#ifdef __APPLE__
    io.Fonts->AddFontFromFileTTF("../Resources/font.ttf", 24.0f);
    fontMarquee = io.Fonts->AddFontFromFileTTF("../Resources/font.ttf", 34.0f);
    fontBig = io.Fonts->AddFontFromFileTTF("../Resources/font.ttf", 54.0f);
#else
    io.Fonts->AddFontFromFileTTF("font.ttf", 24.0f);
    fontMarquee = io.Fonts->AddFontFromFileTTF("font.ttf", 34.0f);
    fontBig = io.Fonts->AddFontFromFileTTF("font.ttf", 54.0f);
#endif
    DEJAVISUI_LOG_DEBUG("Finished initializing ImGui Vulkan backend");
}

void CRenderer::GUI_Marquee(std::string _id,ImVec2 pos, std::string text, int font_size) {
    ImGui::PushFont(fontMarquee);
    ImVec2 textsize = ImGui::CalcTextSize(text.c_str());
    float textWidth = textsize.x;
    float textHeight = textsize.y;
    float windowWidth = (float)m_master_per_frame[0].image.width;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, textHeight+5.0)); // Un po' più alto per sicurezza
    ImGui::Begin(_id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);


    const char* marqueeText = text.c_str();

    float availableWidth = windowWidth - 40.0f; // Padding laterale di 20px per parte

    if (textWidth <= availableWidth) {
        float centerX = (windowWidth - textWidth) * 0.5f;
        ImGui::SetCursorPosX(centerX);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.8f), "%s", marqueeText);
    } else {
        static float timer = 0.0f;
        float speed = 0.5f;
        float pauseTime = 1.0f;

        timer += ImGui::GetIO().DeltaTime * speed;

        float maxScroll = textWidth - availableWidth;

        float pingPong = (sinf(ImGui::GetTime() * speed) * 0.5f) + 0.5f;

        float smoothPingPong = pingPong * pingPong * (3.0f - 2.0f * pingPong);

        float scrollX = 20.0f - (maxScroll * smoothPingPong);

        ImGui::SetCursorPosX(scrollX);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.8f), "%s", marqueeText);
    }

    ImGui::End();
    ImGui::PopFont();
}

void CRenderer::ImGui_Welcome_Message() {

    if (imgui_welcome_timerNotify <= 0.0f) {
        return;
    }

    float alpha = 1.0f;
    float tempo_dissolvenza = 1.5f;
    if (imgui_welcome_timerNotify < tempo_dissolvenza) {
        alpha = imgui_welcome_timerNotify / tempo_dissolvenza;
        videoMixerTextures[0].alpha = 1.0f - alpha;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImVec2 textSize = ImGui::CalcTextSize(imgui_welcome_msg);

    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    ImVec2 center_pos = ImVec2(core_w*0.5, core_h * 0.5f);

    ImGui::SetNextWindowPos(center_pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));


    ImGui::Begin("##Welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(fontBig);
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", imgui_welcome_msg);
    ImGui::Separator();
    ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, alpha), "GPU: %s", activeGPUname.c_str());
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, alpha), "Master Audio Device: %s %d KHz", m_audio->AUDIO_MIXER.getMixerOutputItem(0)->audio_dev_name.c_str(),m_audio->AUDIO_MIXER.getMixerOutputItem(1)->samplerate);
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, alpha), "Aux Audio Device: %s %d KHz", m_audio->AUDIO_MIXER.getMixerOutputItem(1)->audio_dev_name.c_str(),m_audio->AUDIO_MIXER.getMixerOutputItem(1)->samplerate);

    imgui_welcome_timerNotify -= ImGui::GetIO().DeltaTime;

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (imgui_welcome_timerNotify <= 0.0f) {
        imgui_welcome_timerNotify = 0.0f;
        videoMixerTextures[0].alpha = 1.0f;
    }

}


void CRenderer::GUI_Render() {

    ImGui_Welcome_Message();

}

void CRenderer::ImGui_Render() {


    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    GUI_Render();

    ImGui::Render();
}
