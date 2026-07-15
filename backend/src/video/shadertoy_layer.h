#ifndef DEJAVIS_UI_SHADERTOY_LAYER_H
#define DEJAVIS_UI_SHADERTOY_LAYER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "vulkan_utils.h"
#include "render_globals.h"
#include <stdlib.h>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <mutex>

struct FragShadersPushConstants {
    glm::vec3 iRes;
    float time;
};

struct TextureBinding {
    VulkanTexture texture;
    VkSampler sampler;
};

class CShaderToy {
public:
    CShaderToy();

    std::string ShaderToy_Comapt_Shader(std::string shader_code);

    static constexpr uint32_t FFT_SIZE = 512;

    void CreateRenderPass();

    void Init(VulkanContext * _ctx, std::string &frag_shader,int _slotid,int _w ,int _h, VulkanTexture * _targetIamge);
    void Cleanup();
    void BindAndDraw(VkCommandBuffer cmd);

    VkDescriptorPool Vulkan_CreateDescriptorPool(VulkanContext *ctx, uint32_t uniformCount, uint32_t samplerCount,
                                                 uint32_t maxSets);

    void UpdateAudioTexture(VkCommandBuffer cmd,const std::array<float, FFT_SIZE>& fftData_Left,const std::array<float, FFT_SIZE>& fftData_Right);

    void Compute(VkCommandBuffer cmd, const FragShadersPushConstants& pc) ;

    bool UpdateShader(std::vector<uint32_t> _spirvcode);

    bool TestShader(std::string _frag_shader);

    std::string& GetCurrentShader(){
        return current_frag_shader;
    }

    std::mutex m_shaderMutex;
    std::string m_pendingShaderSource;
    bool m_hasPendingShader = false;

    void PushNewShader(const std::string& source) {
        std::lock_guard<std::mutex> lock(m_shaderMutex);
        m_pendingShaderSource = source;
        m_hasPendingShader = true;
    }

    std::string current_frag_shader = "";

    VkRenderPass getRenderPass() {
        if (outTexture) {
            return outTexture->renderPass;
        }
        return VK_NULL_HANDLE;
    }

    VkFramebuffer getFramebuffer() {
        if (outTexture) {
            return outTexture->framebuffer;
        }
        return VK_NULL_HANDLE;
    }

    int getSlotID() {
        return slotid;
    }

    VkDescriptorSet getOutputDescriptorSet() {
        return m_outputDescriptorSet;
    }

    void SetiChannelTexture(VulkanUniTexture *_texture,int chanidx);

private:
    VulkanContext* m_ctx;

    std::vector<TextureBinding> m_channels{ 4 };


    VkBuffer         m_uboBuffer;
    VkDeviceMemory   m_uboMemory;
    void*            m_uboMappedPtr{nullptr};



    struct ShadertoyUBO {
        alignas(16) float iResolution[3];
        float _pad_res;
        alignas(4)  float iTime;
        alignas(4)  float iTimeDelta;
        alignas(4)  int   iFrame;
        float _pad_mouse;

        alignas(16) float iMouse[4];
        alignas(16) float iDate[4];
        alignas(4)  float iSampleRate;
    };
    ShadertoyUBO m_uboData;

    ShadertoyUBO prev_m_uboData;

    VkPipelineLayout m_graphicsPipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};

    VkPipelineLayout m_skyPipelineLayout{VK_NULL_HANDLE};

    VkPipeline m_skyPipeline{VK_NULL_HANDLE};

    VkBuffer m_fftBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_fftMem{VK_NULL_HANDLE};
    void* m_fftDataPtr{nullptr};

    FragShadersPushConstants currentpc;
    FragShadersPushConstants prev_currentpc;

    void SetupGraphicsPipeline(std::vector<uint32_t> _spirv_vertx , std::vector<uint32_t> _spirv_frag);
    void SetupSkyPipeline();

    VulkanTexture m_skyHDR;
    VkSampler m_skySampler{VK_NULL_HANDLE};

    VulkanTexture * outTexture;

    VkDescriptorSetLayout m_outputDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_outputDescriptorSet{VK_NULL_HANDLE};



    int slotid = -1;
    std::string buildShaderSource(const std::string& shadertoyCode);

    void BindResources(const std::vector<TextureBinding>& textures);
    void UpdateDescriptorSet(
            VkDescriptorSet descriptorSet,
            VkBuffer uboBuffer,
            const std::vector<TextureBinding>& bindings
    );

    void AllocateAndUpdateInputDescriptorSet();


    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};

    void UpdateUBO();

    void CreateDescriptorPool();

    void imageBarrier(VkCommandBuffer cmd, VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage);

    bool CreateImageResources(VulkanTexture *out, uint32_t w, uint32_t h);

    void DestroyImageResources(MasterResources *out);

    VkShaderModule loadShader(const char *glslSource, const char *name);

    VkShaderModule CreateShaderModule(const uint32_t *code, size_t sizeInBytes);

    std::vector<uint32_t> spirv_vertex_constant;

};

#endif //DEJAVIS_UI_SHADERTOY_LAYER_H