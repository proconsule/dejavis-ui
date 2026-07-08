#include "renderer.h"


const char* mixer_vertexshader_source = R"(
#version 450
layout(push_constant) uniform LayerLayout {
    vec2 position;
    vec2 scale;
    float alpha;
    int yFlip;
    int useBicubic;
} push;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float fragAlpha;
layout(location = 2) out vec2 fragScale;

vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
    vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0)
);

vec2 texCoords[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex] * push.scale + push.position, 0.0, 1.0);

    vec2 coords = texCoords[gl_VertexIndex];
    if (push.yFlip == 1) {
        coords.y = 1.0 - coords.y;
    }
    fragTexCoord = coords;
    fragAlpha = push.alpha;
    fragScale = push.scale;
}
)";

const char* mixer_fragshader_source = R"(
#version 450
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float fragAlpha;
layout(location = 2) in vec2 fragScale;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform LayerLayout {
    vec2 position;
    vec2 scale;
    float alpha;
    int yFlip;
    int useBicubic;
} push;

float cubic(float x) {
    float a = -0.5;
    float absX = abs(x);
    if (absX <= 1.0) {
        return (a + 2.0) * pow(absX, 3.0) - (a + 3.0) * pow(absX, 2.0) + 1.0;
    } else if (absX < 2.0) {
        return a * pow(absX, 3.0) - 5.0 * a * pow(absX, 2.0) + 8.0 * a * absX - 4.0 * a;
    }
    return 0.0;
}

void main() {
    if (push.useBicubic == 0) {
        vec4 texColor = texture(texSampler, fragTexCoord);
        outColor = vec4(texColor.rgb, texColor.a * fragAlpha);
        return;
    }

    vec2 texSize = vec2(textureSize(texSampler, 0));
    vec2 pixelPos = fragTexCoord * texSize - 0.5;

    vec2 f = fract(pixelPos);
    vec2 i = floor(pixelPos);

    vec2 w0 = 0.5 * f * (f - 1.0) * (f - 2.0);

    vec2 texelSize = 1.0 / texSize;

    vec2 coord1 = (i + vec2(0.5, 0.5)) * texelSize;
    vec2 coord2 = (i + vec2(1.5, 0.5)) * texelSize;
    vec2 coord3 = (i + vec2(0.5, 1.5)) * texelSize;
    vec2 coord4 = (i + vec2(1.5, 1.5)) * texelSize;

    vec4 color = texture(texSampler, coord1) * ((1.0 - f.x) * (1.0 - f.y)) +
                 texture(texSampler, coord2) * (f.x * (1.0 - f.y)) +
                 texture(texSampler, coord3) * ((1.0 - f.x) * f.y) +
                 texture(texSampler, coord4) * (f.x * f.y);

    outColor = vec4(color.rgb, color.a * fragAlpha);
}

)";

// Struct per matchare il Vertex Shader
struct LayerLayout {
    float position[2];
    float scale[2];
    float alpha;
    int yFlip;         // 1 per attivare il flip, 0 altrimenti
    int useBicubic;
    float padding;
};

bool CRenderer::initVideoMixer() {
    DEJAVISUI_LOG_DEBUG("Inizializzazione Video Mixer...");

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    // 1. COMPILAZIONE SHADERS
    // --- Vertex ---
    auto vertModule = compiler.CompileGlslToSpv(mixer_vertexshader_source, shaderc_glsl_vertex_shader, "mixer.vert", options);
    if (vertModule.GetCompilationStatus() != shaderc_compilation_status_success) {
        DEJAVISUI_LOG_ERROR("Mixer Vertex Error: %s", vertModule.GetErrorMessage().c_str());
        return false;
    }
    std::vector<uint32_t> vertSpirv(vertModule.cbegin(), vertModule.cend());
    VkShaderModule vertShader = CreateShaderModule(vertSpirv.data(), vertSpirv.size() * sizeof(uint32_t));

    // --- Fragment ---
    auto fragModule = compiler.CompileGlslToSpv(mixer_fragshader_source, shaderc_glsl_fragment_shader, "mixer.frag", options);
    if (fragModule.GetCompilationStatus() != shaderc_compilation_status_success) {
        DEJAVISUI_LOG_ERROR("Mixer Fragment Error: %s", fragModule.GetErrorMessage().c_str());
        return false;
    }
    std::vector<uint32_t> fragSpirv(fragModule.cbegin(), fragModule.cend());
    VkShaderModule fragShader = CreateShaderModule(fragSpirv.data(), fragSpirv.size() * sizeof(uint32_t));

    // 2. DESCRIPTOR SET LAYOUT (Binding 0: Sampler)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_ctx.device, &layoutInfo, nullptr, &m_ctx.m_mixerDescriptorLayout) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Errore creazione DescriptorSetLayout Mixer");
        return false;
    }

    // 3. PIPELINE LAYOUT (Push Constants)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LayerLayout); // Assicurati che LayerLayout sia definita (2 float pos, 2 float scale)

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_ctx.m_mixerDescriptorLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_ctx.device, &pipelineLayoutInfo, nullptr, &m_mixerPipelineLayout) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Errore creazione PipelineLayout Mixer");
        return false;
    }

    // 4. GRAPHICS PIPELINE CONFIGURATION
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";

    // Stati fissi (Empty Vertex Input perché usiamo gl_VertexIndex)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Visualizziamo entrambi i lati per sicurezza

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- ALPHA BLENDING: Il cuore del mixer ---
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF; // RGBA
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 5. CREAZIONE PIPELINE
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_mixerPipelineLayout;
    pipelineInfo.renderPass = m_videoBusResources[0].m_master_per_frame[0].renderPass; // Aggancio al RenderPass del Core
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_mixerPipeline) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Errore creazione Graphics Pipeline Mixer");
        return false;
    }

    // Cleanup moduli shader (Vulkan li ha già compilati internamente nella pipeline)
    vkDestroyShaderModule(m_ctx.device, vertShader, nullptr);
    vkDestroyShaderModule(m_ctx.device, fragShader, nullptr);

    DEJAVISUI_LOG_DEBUG("Video Mixer Pipeline inizializzata correttamente.");
    return true;
}

void CRenderer::drawTestOverlay(VkCommandBuffer cmd, VkDescriptorSet textureSet) {

    auto& cur = m_videoBusResources[0].m_master_per_frame[m_display.currentFrame];

    // 1. SICUREZZA: Se la pipeline o il set non esistono, usciamo per evitare crash
    if (m_mixerPipeline == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("Pipeline Mixer non inizializzata!");
        return;
    }
    if (textureSet == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("Descriptor Set della texture nullo in drawTestOverlay!");
        return;
    }

    // 2. BIND PIPELINE: Specifichiamo quale shader e stati usare
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mixerPipeline);

    // 3. STATI DINAMICI: Fondamentali perché li abbiamo dichiarati come DYNAMIC nella pipeline.
    // Senza questi, la GPU non sa "dove" disegnare nel framebuffer.

    // Viewport: definisce la mappatura coordinate -> pixel
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)cur.image.width;
    viewport.height = (float)cur.image.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Scissor: definisce l'area di ritaglio (solitamente uguale alla viewport)
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = { cur.image.width, cur.image.height };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 4. PUSH CONSTANTS: Posizionamento del layer.
    // Qui definiamo il rettangolo:
    // position (0.5, -0.5) -> In alto a destra
    // scale (0.5, 0.5)     -> Grande un quarto dell'area totale
    LayerLayout push;
    push.position[0] = 0.5f;
    push.position[1] = -0.5f;
    push.scale[0]    = 0.5f;
    push.scale[1]    = 0.5f;

    vkCmdPushConstants(cmd, m_mixerPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LayerLayout), &push);

    // 5. BIND DESCRIPTOR SET: Collega la texture di ProjectM allo shader
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mixerPipelineLayout, 0, 1, &textureSet, 0, nullptr);

    // 6. DRAW: Disegna i 6 vertici (2 triangoli) definiti nell'array 'positions' dello shader
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

/*

VkDescriptorSet CRenderer::createTextureDescriptor(VkImageView imageView) {
    if (imageView == VK_NULL_HANDLE || m_defaultSampler == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("Impossibile creare descrittore: View o Sampler nulli");
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_ctx.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_ctx.m_mixerDescriptorLayout;

    if (vkAllocateDescriptorSets(m_ctx.device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = m_defaultSampler;

    VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_ctx.device, 1, &descriptorWrite, 0, nullptr);

    return descriptorSet;
}

*/

void CRenderer::drawMixerVideoLayer(VkCommandBuffer cmd,
                                    videomixeritem* _mixerprop,
                                    VulkanUniTexture& _texture) { // Parametro aggiunto



    auto& cur = m_videoBusResources[0].m_master_per_frame[m_display.currentFrame];
    if (!_mixerprop->inUse) return;

    float finalScaleX = _mixerprop->scale_x;
    float finalScaleY = _mixerprop->scale_y;

    bool usedecoderimage = (_mixerprop->AV_DECODER != nullptr);
    bool useimageviewverimage = (_mixerprop->img_viewver != nullptr);
    bool useurldecoderimage = (_mixerprop->AV_STREAM_DECODER != nullptr);
    bool usendi = (_mixerprop->ndi_receiver != nullptr);

    // --- LOGICA COMUNE PER ASPECT RATIO ---
    auto calculateAspect = [&](float texW, float texH, float& outX, float& outY) {
        if (texW <= 0 || texH <= 0 || cur.image.width <= 0 || cur.image.height <= 0) return;
        float textureRatio = texW / texH;
        float canvasRatio  = (float)cur.image.width / (float)cur.image.height;
        float baseScale = _mixerprop->scale_x;
        if (textureRatio > canvasRatio) {
            outX = baseScale;
            outY = baseScale * (canvasRatio / textureRatio);
        } else {
            outY = baseScale;
            outX = baseScale * (textureRatio / canvasRatio);
        }
    };

    if (_mixerprop->originalIdx == 0) { //should be projectM
        if (m_projectm_wrapper) {

            VkDescriptorSet ds = m_projectm_wrapper->getMixerDescriptorSet();
            drawVideoLayer(cmd, ds,
                       _mixerprop->pos_x, _mixerprop->pos_y,
                       finalScaleX, finalScaleY,
                       _mixerprop->alpha, _mixerprop->y_flip,_mixerprop->useBicubic);

        }
    }else if (usedecoderimage) {
        if (_mixerprop->AV_DECODER->getOutputWidth() > 0 && _mixerprop->AV_DECODER->getOutputHeight() > 0) {
            if (_mixerprop->keepaspect) {
                calculateAspect((float)_mixerprop->AV_DECODER->getOutputWidth(),
                                (float)_mixerprop->AV_DECODER->getOutputHeight(),
                                finalScaleX, finalScaleY);
            }

            // RECUPERO DESCRITTORE INTELLIGENTE
            VkDescriptorSet ds;
            CPostProcessor* fx = _mixerprop->AV_DECODER->getPostProcessor();


            ds = _mixerprop->AV_DECODER->getMixerDescriptorSet();


            drawVideoLayer(cmd, ds,
                       _mixerprop->pos_x, _mixerprop->pos_y,
                       finalScaleX, finalScaleY,
                       _mixerprop->alpha, _mixerprop->y_flip,_mixerprop->useBicubic);
        }
    } else if (usendi) {
        if (_mixerprop->ndi_receiver->getOutputWidth() > 0) {
            if (_mixerprop->keepaspect) {
                calculateAspect((float)_mixerprop->ndi_receiver->getOutputWidth(),
                                (float)_mixerprop->ndi_receiver->getOutputHeight(),
                                finalScaleX, finalScaleY);
            }
            // RECUPERO DESCRITTORE INTELLIGENTE
            VkDescriptorSet ds;
            CPostProcessor* fx = _mixerprop->ndi_receiver->getPostProcessor();


            ds = fx->getOutputDescriptorSet();

            drawVideoLayer(cmd, ds,
                           _mixerprop->pos_x, _mixerprop->pos_y,
                           finalScaleX, finalScaleY,
                           _mixerprop->alpha, _mixerprop->y_flip,_mixerprop->useBicubic);
        }
    } else if (useurldecoderimage) {
        if (_mixerprop->AV_STREAM_DECODER->getOutputWidth() > 0) {
            if (_mixerprop->keepaspect) {
                calculateAspect((float)_mixerprop->AV_STREAM_DECODER->getOutputWidth(),
                                (float)_mixerprop->AV_STREAM_DECODER->getOutputHeight(),
                                finalScaleX, finalScaleY);
            }
            drawVideoLayer(cmd, _mixerprop->AV_STREAM_DECODER->getMixerDescriptorSet(),
                       _mixerprop->pos_x, _mixerprop->pos_y,
                       finalScaleX, finalScaleY,
                       _mixerprop->alpha, _mixerprop->y_flip,_mixerprop->useBicubic);
        }
    } else if (useimageviewverimage) {
        if (_mixerprop->img_viewver->getOutputWidth() > 0) {
            if (_mixerprop->keepaspect) {
                calculateAspect((float)_texture.VkTexture.width, (float)_texture.VkTexture.height,
                                finalScaleX, finalScaleY);
            }
            VkDescriptorSet ds = _mixerprop->img_viewver->getMixerDescriptorSet();
            drawVideoLayer(cmd, ds,
                       _mixerprop->pos_x, _mixerprop->pos_y,
                       finalScaleX, finalScaleY,
                       _mixerprop->alpha, _mixerprop->y_flip,_mixerprop->useBicubic);
        }
    }
}

void CRenderer::drawVideoLayer(VkCommandBuffer cmd, VkDescriptorSet textureSet,
                               float x, float y, float scaleX, float scaleY,
                               float alpha, bool yFlip,bool useBicubic) {
    if (m_mixerPipeline == VK_NULL_HANDLE || textureSet == VK_NULL_HANDLE) return;

    // Push Constants
    LayerLayout push{};
    push.position[0] = x;
    push.position[1] = y;
    push.scale[0]    = scaleX;
    push.scale[1]    = scaleY;
    push.alpha       = alpha;
    push.yFlip       = yFlip ? 1 : 0;
    push.useBicubic   = useBicubic ? 1 : 0;

    vkCmdPushConstants(cmd, m_mixerPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LayerLayout), &push);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mixerPipelineLayout, 0, 1, &textureSet, 0, nullptr);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void CRenderer::ProcessVideoMixer_PreRenderPass(VkCommandBuffer cmd) {
    for (int i = 0; i < 10; ++i) {
        videomixeritem* p = &videoMixerTextures[i];
        if (!p->inUse) continue;            // ← gating unico, identico al drain
        if (!p->isVisible) continue;

        if (i == 0 && m_projectm_wrapper) {

            if (auto* pp = m_projectm_wrapper->getPostProcessor()) pp->submit();
        }

        if (p->ndi_receiver) {
            p->ndi_receiver->processTextureLifecycle();
            //p->ndi_receiver->updateTextureFromStaging(cmd);
            if (auto* pp = p->ndi_receiver->getPostProcessor()) pp->submit();
        }
        if (p->AV_DECODER) {
            p->AV_DECODER->submitPostProcess();
        }
        if (p->img_viewver) {
            if (p->img_viewver->needUpdate) {
                if (auto* pp = p->img_viewver->getPostProcessor()) pp->submit();
                p->img_viewver->needUpdate = false;
            }
        }
    }
}

void CRenderer::ProcessVideoMixer(VkCommandBuffer cmd,int _busIdx) {

    std::vector<videomixeritem *> sortedPtrs;
    for(int i = 0; i < 10; ++i) {
        if (videoMixerTextures[i].busoutIdx == _busIdx) {
            sortedPtrs.push_back(&videoMixerTextures[i]);
        }
    }
    std::stable_sort(std::begin(sortedPtrs), std::end(sortedPtrs),
        [](videomixeritem* a, videomixeritem* b) {
            return a->layer < b->layer;
        });
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mixerPipeline);

    auto& cur = m_videoBusResources[0].m_master_per_frame[m_display.currentFrame];

    //vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mixerPipeline);

    // Viewport e Scissor dinamici
    VkViewport viewport{ 0.0f, 0.0f, (float)cur.image.width, (float)cur.image.height, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, { cur.image.width, cur.image.height } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);


    for(int i = 0; i < sortedPtrs.size(); ++i) {
        videomixeritem* p = sortedPtrs[i];
        //if (p->img_viewver) {
            //p->img_viewver->Render(cmd);
        //}
        if(p->inUse && p->isVisible) {
            drawMixerVideoLayer(cmd,p,videoTextures[p->originalIdx]);

        }
    }



}

void CRenderer::SetVideoMixerProps(videomixeritem &prop,int _mixerid) {
    videoMixerTextures[_mixerid].pos_x = prop.pos_x;
    videoMixerTextures[_mixerid].pos_y = prop.pos_y;
    videoMixerTextures[_mixerid].scale_x = prop.scale_x;
    videoMixerTextures[_mixerid].scale_y = prop.scale_y;
    videoMixerTextures[_mixerid].alpha = prop.alpha;
    videoMixerTextures[_mixerid].layer = prop.layer;


}

videomixeritem CRenderer::GetTestVideoMixer() {
    return videoMixerTextures[0];
}

int CRenderer::FindFreeVideoMixerSlot() {

    for (int i=0;i<10;i++) {
        if (videoMixerTextures[i].inUse == false) {
            return i;
        }
    }
    return -1;
}

void CRenderer::RemoveVideoFilePlayerFromMixer(int _audio_mixer_id) {
    DEJAVISUI_LOG_DEBUG("REMOVING VIDEOFILEPLAYER FROM AUDIO MIXERID: %d",_audio_mixer_id);

    CAV_DECODER* decoderToDelete = nullptr;
    int videoslot = m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx;

    {
        std::lock_guard<std::mutex> lock(m_videoMixerMutex);

        decoderToDelete = videoMixerTextures[videoslot].AV_DECODER;
        videoMixerTextures[videoslot].AV_DECODER = nullptr;
        int originalidx = videoMixerTextures[videoslot].originalIdx;
        videoMixerTextures[videoslot] = videomixeritem();
        videoMixerTextures[videoslot].originalIdx = originalidx;

    }
    if (decoderToDelete) {
        decoderToDelete->cleanup();   // ferma i thread PRIMA del ring audio
    }
    m_audio->AUDIO_MIXER.RemoveGenericToMixer(_audio_mixer_id);  // libera ring
    if (decoderToDelete) {
        delete decoderToDelete;       // distruttore quasi no-op (cleanup gia' fatto)
    }


}

int CRenderer::AddVideoURLPlayerToMixer(std::string _path,int _audio_mixer_id) {
    int slot = FindFreeVideoMixerSlot();
    if (slot < 0) {
        return -1;
    }

    videoMixerTextures[slot].inUse = true;
    videoMixerTextures[slot].layer = 0;
    videoMixerTextures[slot].isVisible = true;
    videoMixerTextures[slot].type = 4;
    m_audio->AUDIO_MIXER.AddGenericToMixer(_audio_mixer_id);
    m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx = slot;
    videoMixerTextures[slot].AV_STREAM_DECODER = new CAV_DECODER();
    //videoMixerTextures[slot].AV_DECODER->SetFileBroswerBasePath(_path);
    videoMixerTextures[slot].AV_STREAM_DECODER->InitDecoder(&m_ctx,m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->buffer_planar.get(),48000,2);
    videoMixerTextures[slot].AV_STREAM_DECODER->LoadFileAsync(_path);


    return slot;
}

bool CRenderer::AddNDIToMixer(int _audio_mixer_id) {
    int slot = FindFreeVideoMixerSlot();
    if (slot < 0) {
        return -1;
    }

    videoMixerTextures[slot].inUse = true;
    videoMixerTextures[slot].layer = 0;
    videoMixerTextures[slot].isVisible = true;
    videoMixerTextures[slot].type = 5;
    videoMixerTextures[slot].audiomixerid = _audio_mixer_id;
    m_audio->AUDIO_MIXER.AddGenericToMixer(_audio_mixer_id);
    m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx = slot;
    m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->type = 5;
    videoMixerTextures[slot].ndi_receiver = new CNDIReceiver();
    videoMixerTextures[slot].ndi_receiver->Init_VideoAudio(&m_ctx,&videoTextures[slot],m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->buffer_planar.get(),48000,2);
    return true;
}

void CRenderer::RemoveNDIFromVideoMixerID(int _video_mixer_id) {
    CNDIReceiver* decoderToDelete = nullptr;
    int audiomixerid = videoMixerTextures[_video_mixer_id].audiomixerid;
    {
        std::lock_guard<std::mutex> lock(m_videoMixerMutex);

        decoderToDelete = videoMixerTextures[_video_mixer_id].ndi_receiver;
        videoMixerTextures[_video_mixer_id].ndi_receiver = nullptr;
        int originalidx = videoMixerTextures[_video_mixer_id].originalIdx;
        videoMixerTextures[_video_mixer_id] = videomixeritem();
        videoMixerTextures[_video_mixer_id].originalIdx = originalidx;

    }
    m_audio->AUDIO_MIXER.RemoveGenericToMixer(audiomixerid);
    if (decoderToDelete) {
        delete decoderToDelete;
    }
}

void CRenderer::RemoveNDIFromAudioMixerID(int _audio_mixer_id) {
    int videoslot = m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx;
    CNDIReceiver* decoderToDelete = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_videoMixerMutex);

        decoderToDelete = videoMixerTextures[videoslot].ndi_receiver;
        videoMixerTextures[videoslot].ndi_receiver = nullptr;
        int originalidx = videoMixerTextures[videoslot].originalIdx;
        videoMixerTextures[videoslot] = videomixeritem();
        videoMixerTextures[videoslot].originalIdx = originalidx;

    }
    m_audio->AUDIO_MIXER.RemoveGenericToMixer(_audio_mixer_id);
    if (decoderToDelete) {
        delete decoderToDelete;
    }
}

int CRenderer::AddVideoFilePlayerToMixer(std::string _path,int _audio_mixer_id) {
    int slot = FindFreeVideoMixerSlot();
    if (slot < 0) {
        return -1;
    }

    videoMixerTextures[slot].inUse = true;
    videoMixerTextures[slot].layer = 0;
    videoMixerTextures[slot].isVisible = true;
    videoMixerTextures[slot].type = 2;
    m_audio->AUDIO_MIXER.AddGenericToMixer(_audio_mixer_id);
    m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx = slot;
    videoMixerTextures[slot].AV_DECODER = new CAV_DECODER();
    videoMixerTextures[slot].AV_DECODER->SetFileBroswerBasePath(_path);
    videoMixerTextures[slot].AV_DECODER->InitDecoder(&m_ctx,m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->buffer_planar.get(),48000,2);
    videoMixerTextures[slot].AV_DECODER->InitFFmpegVulkanHW(ffmpeg_vk_ctx);
    //videoMixerTextures[slot].AV_DECODER->m_yuvcompute = &videoComputes[slot];
    //videoMixerTextures[slot].AV_DECODER->m_vulkan_texture = &videoTextures[slot];



    //videoMixerTextures[slot].AV_DECODER->LoadFile("C:\\Users\\proco\\Downloads\\Depeche Mode - Ghosts Again.mp4");
    /*
    videoMixerTextures[slot].AV_DECODER->open(_path,m_audio->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->buffer.get(),48000,2);


    InitYUVToRGBAComputePipeline(videoComputes[slot],videoTextures[slot]);
    CreateYUVResources(&videoComputes[slot],videoMixerTextures[slot].AV_DECODER->getVideoCtx()->width,videoMixerTextures[slot].AV_DECODER->getVideoCtx()->height);
    CreateYUVToRGBAResources(videoComputes[slot],videoTextures[slot]);
    */
    return slot;
}

bool CRenderer::AddImageToMixer(const unsigned char* img_data,int img_size,bool isHDR) {
    int slot = FindFreeVideoMixerSlot();
    if (slot < 0) {
        return false;
    }
    videoMixerTextures[slot].img_viewver = new cimage_viewer();
    videoMixerTextures[slot].img_viewver->Init(&m_ctx);
    videoMixerTextures[slot].img_viewver->Vulkan_LoadTexture_FromMemory(&videoTextures[slot],img_data, img_size,isHDR);
    videoMixerTextures[slot].inUse = true;
    videoMixerTextures[slot].busoutIdx = 0;
    videoMixerTextures[slot].layer = 0;
    videoMixerTextures[slot].isVisible = true;
    videoMixerTextures[slot].type = 1;
    return true;
}

void CRenderer::RemoveImageFromMixer(int slot) {
    std::lock_guard<std::mutex> lock(m_videoMixerMutex);
    if (slot > 0 && slot < 10) {
        int originalidx = videoMixerTextures[slot].originalIdx;
        CleanupTexture(videoTextures[slot].VkTexture);
        videoMixerTextures[slot] = videomixeritem();
        videoMixerTextures[slot].originalIdx = originalidx;

    }
}

void CRenderer::SetKeyer(int _mixeridx,FxKeyerMode _keyer) {
    std::lock_guard<std::mutex> lock(m_videoMixerMutex);
    videoMixerTextures[_mixeridx].keyerMode = FxKeyerMode::Luma;
    if (_mixeridx >=0 && _mixeridx < 10) {
        if(videoMixerTextures[_mixeridx].AV_DECODER) {
            videoMixerTextures[_mixeridx].AV_DECODER->setKeyer(_keyer);

        }
        if(videoMixerTextures[_mixeridx].img_viewver) {
            videoMixerTextures[_mixeridx].img_viewver->setKeyer(_keyer);
        }
        if(videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setKeyer(_keyer);
        }
        if(videoMixerTextures[_mixeridx].ndi_receiver) {
            videoMixerTextures[_mixeridx].ndi_receiver->setKeyer(_keyer);
        }
    }

}

void CRenderer::SetLumaKey(int _mixeridx,LumaKeyParams &params) {

    if(params.enabled >0.0) {
        SetKeyer(_mixeridx,FxKeyerMode::Luma);
    }

    std::lock_guard<std::mutex> lock(m_videoMixerMutex);

    if (_mixeridx >=0 && _mixeridx < 10) {

        if(_mixeridx == 0) { //projectM
            if (m_projectm_wrapper) {
                m_projectm_wrapper->setLumaKey(params);
            }
        }

        videoMixerTextures[_mixeridx].lumaParams = params;
        if(videoMixerTextures[_mixeridx].AV_DECODER) {
            videoMixerTextures[_mixeridx].AV_DECODER->setLumaKey(params);

        }
        if(videoMixerTextures[_mixeridx].img_viewver) {
            videoMixerTextures[_mixeridx].img_viewver->setLumaKey(params);

        }
        if(videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setLumaKey(params);

        }
        if(videoMixerTextures[_mixeridx].ndi_receiver) {
            videoMixerTextures[_mixeridx].ndi_receiver->setLumaKey(params);

        }


    }
}

void CRenderer::SetChromaKey(int _mixeridx,KeyerPushConstants &params) {

    if(params.enabled >0.0) {
        SetKeyer(_mixeridx,FxKeyerMode::Chroma);
    }

    std::lock_guard<std::mutex> lock(m_videoMixerMutex);

    if (_mixeridx >=0 && _mixeridx < 10) {

        videoMixerTextures[_mixeridx].chromaParams = params;

        if(_mixeridx == 0) { //projectM
            if (m_projectm_wrapper) {
                m_projectm_wrapper->setChromaKey(params);
            }
        }

        if(videoMixerTextures[_mixeridx].AV_DECODER) {
            videoMixerTextures[_mixeridx].AV_DECODER->setChromaKey(params);
        }
        if(videoMixerTextures[_mixeridx].img_viewver) {
            videoMixerTextures[_mixeridx].img_viewver->setChromaKey(params);
        }
        if(videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setChromaKey(params);
        }
        if(videoMixerTextures[_mixeridx].ndi_receiver) {
            videoMixerTextures[_mixeridx].ndi_receiver->setChromaKey(params);
        }

    }
}

void CRenderer::SetColor(int _mixeridx, ColorParams &params) {
    std::lock_guard<std::mutex> lock(m_videoMixerMutex);
    if (_mixeridx >=0 && _mixeridx < 10) {

        if(_mixeridx == 0) { //projectM
            if (m_projectm_wrapper) {
                m_projectm_wrapper->setColor(params);
            }
        }

        videoMixerTextures[_mixeridx].colorParams = params;
        if(videoMixerTextures[_mixeridx].AV_DECODER) {
            videoMixerTextures[_mixeridx].AV_DECODER->setColor(params);
        }
        if(videoMixerTextures[_mixeridx].img_viewver) {
            videoMixerTextures[_mixeridx].img_viewver->setColor(params);
        }
        if(videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setColor(params);
        }

    }
}


void CRenderer::VideoMixer_SyncInputs() {

    if (m_pendingNDI_Unload.shouldUnLoad.load()) {
        m_pendingNDI_Unload.shouldUnLoad.store(false);
        if(m_pendingNDI_Unload.isvideomixer) {
            RemoveNDIFromVideoMixerID(m_pendingNDI_Unload.mixerid);
        }else {
            RemoveNDIFromAudioMixerID(m_pendingNDI_Unload.mixerid);
        }

    }

    if (m_pendingNDI.shouldLoad.load()) {
        m_pendingNDI.shouldLoad.store(false);
        AddNDIToMixer(m_pendingNDI.mixerid);
    }

    if (m_pendingNDISource.shouldLoad.load()) {
        m_pendingNDISource.shouldLoad.store(false);
        CNDIReceiver *receiver = videoMixerTextures[m_pendingNDISource.mixerid].ndi_receiver;
        if (receiver) {
            receiver->Start(m_pendingNDISource.url);
        }

    }

    if (m_pendingImageLoad.shouldLoad.load()) {
        m_pendingImageLoad.shouldLoad.store(false);
        AddImageToMixer(m_pendingImageLoad.data,m_pendingImageLoad.data_size,false);
        if (m_pendingImageLoad.data)free(m_pendingImageLoad.data);

    }

    if (m_pendingImageUnLoad.shouldUnLoad.load()) {
        m_pendingImageUnLoad.shouldUnLoad.store(false);
        RemoveImageFromMixer(m_pendingImageUnLoad.mixerid);

    }

    /*
     *  DELEGATO A UNIMXIER via handleenevts
    if (m_pendingInputLoad.shouldLoad.load()) {
        m_pendingInputLoad.shouldLoad.store(false);
        AddVideoFilePlayerToMixer(m_pendingInputLoad.url,m_pendingInputLoad.mixerid);

    }
    if (m_pendingLoad.shouldLoad.load()) {
        m_pendingLoad.shouldLoad.store(false);
        int videoslot = m_audio->AUDIO_MIXER.getMixerInputItem(m_pendingLoad.mixerid)->videomixer_idx;
        if (videoMixerTextures[videoslot].AV_DECODER) {
            videoMixerTextures[videoslot].AV_DECODER->LoadFileAsync(m_pendingLoad.url);
        }

    }
    if (m_peningUnload.shouldUnLoad.load()) {
        m_peningUnload.shouldUnLoad.store(false);
        RemoveVideoFilePlayerFromMixer(m_peningUnload.mixerid);
    }
    */

    if (m_pendingVideoUrlload.shouldLoad.load()) {
        m_pendingVideoUrlload.shouldLoad.store(false);
        DEJAVISUI_LOG_DEBUG("OPENING URL:%s",m_pendingVideoUrlload.url.c_str());
        AddVideoURLPlayerToMixer(m_pendingVideoUrlload.url,m_pendingVideoUrlload.mixerid);
    }
}



Json::Value CRenderer::GetVideoMixerJson() {
    std::lock_guard<std::mutex> lock(m_videoMixerMutex);
    Json::Value root;
    Json::Value list(Json::arrayValue);
    for (const auto& videoinput : videoMixerTextures) {
        Json::Value item;
        item["inUse"] = videoinput.inUse;
        item["type"] = videoinput.type;
        item["pos_x"] = videoinput.pos_x;
        item["pos_y"] = videoinput.pos_y;
        item["scale_x"] = videoinput.scale_x;
        item["scale_y"] = videoinput.scale_y;
        item["alpha"] = videoinput.alpha;
        item["layer"] = videoinput.layer;
        item["busoutIdx"] = videoinput.busoutIdx;
        CAV_DECODER *decoder = videoinput.AV_DECODER;
        if (decoder && videoinput.originalIdx >-1) {
            item["width"] = decoder->getOutputWidth();
            item["height"] = decoder->getOutputHeight();
        }else {
            item["width"] = videoTextures[videoinput.originalIdx].VkTexture.width;
            item["height"] = videoTextures[videoinput.originalIdx].VkTexture.height;
        }

        item["keepaspect"] = videoinput.keepaspect;
        item["isVisible"] = videoinput.isVisible;
        item["keyerMode"] = (uint32_t)videoinput.keyerMode;
        // Integrazione ChromaKeyParams
        Json::Value chroma;
        chroma["v0"]      = videoinput.chromaParams.v0;
        chroma["v1"]      = videoinput.chromaParams.v1;
        chroma["v2"]      = videoinput.chromaParams.v2;
        chroma["threshold"] = videoinput.chromaParams.threshold;
        chroma["softness"]  = videoinput.chromaParams.softness;
        chroma["spill"]     = videoinput.chromaParams.spill;
        chroma["enabled"]   = videoinput.chromaParams.enabled;
        item["chromakey"]      = chroma;


        Json::Value luma;
        luma["lower"] = videoinput.lumaParams.lower;
        luma["upper"] = videoinput.lumaParams.upper;
        luma["invert"] = videoinput.lumaParams.invert;
        luma["softness"] = videoinput.lumaParams.softness;
        luma["enabled"] = videoinput.lumaParams.enabled;

        item["lumakey"] = luma;

        // Integrazione ColorParams
        Json::Value color;
        color["brightness"] = videoinput.colorParams.brightness;
        color["contrast"]   = videoinput.colorParams.contrast;
        color["saturation"] = videoinput.colorParams.saturation;
        color["gamma"]      = videoinput.colorParams.gamma;
        color["hueShift"]   = videoinput.colorParams.hueShift;
        color["blackLevel"] = videoinput.colorParams.blackLevel;
        color["whiteLevel"] = videoinput.colorParams.whiteLevel;
        color["enabled"]    = videoinput.colorParams.enabled;
        item["color"]       = color;


        if (videoinput.AV_DECODER!=nullptr) {
            Json::Value decoder = videoinput.AV_DECODER->getJsonStatus();
            item["file_decoder"] = decoder;
        }
        if (videoinput.ndi_receiver!=nullptr) {
            Json::Value ndi;
            ndi["sources"] = videoinput.ndi_receiver->listSources();
            ndi["status"] = videoinput.ndi_receiver->getJsonStatus();
            item["ndi"] = ndi;

        }
        if (videoinput.img_viewver!=nullptr) {
            Json::Value image_viewer_json;

            image_viewer_json["width"] = videoinput.img_viewver->getOutputWidth();
            image_viewer_json["height"] = videoinput.img_viewver->getOutputHeight();
            image_viewer_json["filename"] = videoinput.img_viewver->getFileName();
            image_viewer_json["format"] = videoinput.img_viewver->getFormatName();
            item["image_viewer"] = image_viewer_json;

        }
        list.append(item);
    }

    root["inputs"] = list;
    return root;
}

