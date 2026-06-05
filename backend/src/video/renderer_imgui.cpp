#include "renderer.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

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





    // 1. Caricamento Funzioni (Corretto)
    auto loader = [](const char* name, void* user_data) -> PFN_vkVoidFunction {
        return vkGetInstanceProcAddr((VkInstance)user_data, name);
    };
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, loader, (void*)m_ctx.instance);

    // 2. Inizializzazione Backend
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

#else
    io.Fonts->AddFontFromFileTTF("font.ttf", 24.0f);
    fontMarquee = io.Fonts->AddFontFromFileTTF("font.ttf", 34.0f);

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

    // --- CASO 1: Il testo ci sta tutto -> Centrato ---
    if (textWidth <= availableWidth) {
        float centerX = (windowWidth - textWidth) * 0.5f;
        ImGui::SetCursorPosX(centerX);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.8f), "%s", marqueeText);
    }
    // --- CASO 2: Il testo sfora -> Ping-Pong ---
    else {
        static float timer = 0.0f;
        float speed = 0.5f; // Velocità dell'oscillazione (più basso = più lento)
        float pauseTime = 1.0f; // Pausa ai bordi in secondi

        timer += ImGui::GetIO().DeltaTime * speed;

        // Calcoliamo lo sfasamento massimo (quanto "esce" il testo)
        float maxScroll = textWidth - availableWidth;

        // Usiamo un triangolo d'onda per il ping-pong (valore tra 0 e 1)
        // Aggiungiamo una pausa ai bordi usando un clamp sul seno o un modulo
        float pingPong = (sinf(ImGui::GetTime() * speed) * 0.5f) + 0.5f;

        // Applichiamo un pizzico di "smooth step" per rendere il cambio direzione meno brusco
        float smoothPingPong = pingPong * pingPong * (3.0f - 2.0f * pingPong);

        float scrollX = 20.0f - (maxScroll * smoothPingPong);

        ImGui::SetCursorPosX(scrollX);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.8f), "%s", marqueeText);
    }

    ImGui::End();
    ImGui::PopFont();
}

void CRenderer::GUI_Render() {
    // CLEAR
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // Sfondo 0 alpha
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);    // Niente bordi


    auto& cur = m_master_per_frame[m_display.currentFrame];

    // 3. Posiziona il testo
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);

    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "DEJAVIS ENGINE ACTIVE");
    ImGui::Separator();
    ImGui::Text("Mixer Samplerate: %u Hz", m_audio->AUDIO_MIXER.master_samplerate);
    // Qui puoi aggiungere i dati del tuo mixer o del resampler
    ImGui::End();
    // --- MARQUEE BASSO ---
    float windowWidth = (float)cur.image.width;
    float windowHeight = (float)cur.image.height;

    // Posizioniamo la barra in basso (es. 40 pixel dal fondo)
/*
    if (!m_audio->getAudioDecoder()->GetMetadata().title.empty()) {
            ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(20, windowHeight - 115),
        ImVec2(windowWidth-20, windowHeight - 25),
        IM_COL32(0, 0, 0, 150) // Verde semi-trasparente
    );
        GUI_Marquee("marq1",ImVec2(0, windowHeight - 110),m_audio->getAudioDecoder()->GetMetadata().artist);
        GUI_Marquee("marq2", ImVec2(0, windowHeight - 80),m_audio->getAudioDecoder()->GetMetadata().title);
    }else {
        GUI_Marquee("marq1",ImVec2(0, windowHeight - 150),m_audio->getAudioDecoder()->getCurrentFilename());
    }
*/

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();


}




void CRenderer::ImGui_Render() {
    // 1. Inizia il frame ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    GUI_Render();

    // 4. Genera i dati per Vulkan
    ImGui::Render();
}
