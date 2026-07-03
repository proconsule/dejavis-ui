#include "YUVConverterCommon.h"

#include <cstring>
#include <vector>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include "../vulkan_utils.h"

namespace yuvconv {

bool createMappedStorageBuffer(VulkanContext* ctx,
                               VkDeviceSize    size,
                               VkBuffer&       outBuffer,
                               VkDeviceMemory& outMemory,
                               void**          outMapped,
                               VkDeviceSize&   outActualSize) {
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx->device, &bi, nullptr, &outBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx->device, outBuffer, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(ctx, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;

    if (vkAllocateMemory(ctx->device, &ai, nullptr, &outMemory) != VK_SUCCESS) {
        return false;
    }
    if (vkBindBufferMemory(ctx->device, outBuffer, outMemory, 0) != VK_SUCCESS) {
        return false;
    }
    if (vkMapMemory(ctx->device, outMemory, 0, req.size, 0, outMapped) != VK_SUCCESS) {
        return false;
    }
    outActualSize = req.size;
    return true;
}

void destroyMappedBuffer(VulkanContext* ctx,
                         VkBuffer&       buffer,
                         VkDeviceMemory& memory,
                         void**          mapped) {
    if (memory && mapped && *mapped) {
        vkUnmapMemory(ctx->device, memory);
        *mapped = nullptr;
    }
    if (buffer) { vkDestroyBuffer(ctx->device, buffer, nullptr); buffer = VK_NULL_HANDLE; }
    if (memory) { vkFreeMemory(ctx->device, memory, nullptr);   memory = VK_NULL_HANDLE; }
}

// =============================================================================
//  Barriers
// =============================================================================
void imageBarrier(VkCommandBuffer cmd, VkImage img,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void bufferBarrier(VkCommandBuffer cmd, VkBuffer buf,
                   VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                   VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkBufferMemoryBarrier b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer              = buf;
    b.offset              = 0;
    b.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 1, &b, 0, nullptr);
}

// =============================================================================
//  Compute shader compilation
// =============================================================================
static TBuiltInResource defaultResources() {
    TBuiltInResource r{};
    r.maxLights = 32; r.maxClipPlanes = 6; r.maxTextureUnits = 32;
    r.maxTextureCoords = 32; r.maxVertexAttribs = 64;
    r.maxVertexUniformComponents = 4096; r.maxVaryingFloats = 64;
    r.maxVertexTextureImageUnits = 32; r.maxCombinedTextureImageUnits = 80;
    r.maxTextureImageUnits = 32; r.maxFragmentUniformComponents = 4096;
    r.maxDrawBuffers = 32; r.maxVertexUniformVectors = 128;
    r.maxVaryingVectors = 8; r.maxFragmentUniformVectors = 16;
    r.maxVertexOutputVectors = 16; r.maxFragmentInputVectors = 15;
    r.minProgramTexelOffset = -8; r.maxProgramTexelOffset = 7;
    r.maxClipDistances = 8; r.maxComputeWorkGroupCountX = 65535;
    r.maxComputeWorkGroupCountY = 65535; r.maxComputeWorkGroupCountZ = 65535;
    r.maxComputeWorkGroupSizeX = 1024; r.maxComputeWorkGroupSizeY = 1024;
    r.maxComputeWorkGroupSizeZ = 64; r.maxComputeUniformComponents = 1024;
    r.maxComputeTextureImageUnits = 16; r.maxComputeImageUniforms = 8;
    r.maxComputeAtomicCounters = 8; r.maxComputeAtomicCounterBuffers = 1;
    r.maxVaryingComponents = 60; r.maxVertexOutputComponents = 64;
    r.maxGeometryInputComponents = 64; r.maxGeometryOutputComponents = 128;
    r.maxFragmentInputComponents = 128; r.maxImageUnits = 8;
    r.maxCombinedImageUnitsAndFragmentOutputs = 8;
    r.maxCombinedShaderOutputResources = 8; r.maxImageSamples = 0;
    r.maxVertexImageUniforms = 0; r.maxTessControlImageUniforms = 0;
    r.maxTessEvaluationImageUniforms = 0; r.maxGeometryImageUniforms = 0;
    r.maxFragmentImageUniforms = 8; r.maxCombinedImageUniforms = 8;
    r.maxGeometryTextureImageUnits = 16; r.maxGeometryOutputVertices = 256;
    r.maxGeometryTotalOutputComponents = 1024; r.maxGeometryUniformComponents = 1024;
    r.maxGeometryVaryingComponents = 64; r.maxTessControlInputComponents = 128;
    r.maxTessControlOutputComponents = 128; r.maxTessControlTextureImageUnits = 16;
    r.maxTessControlUniformComponents = 1024;
    r.maxTessControlTotalOutputComponents = 4096;
    r.maxTessEvaluationInputComponents = 128; r.maxTessEvaluationOutputComponents = 128;
    r.maxTessEvaluationTextureImageUnits = 16;
    r.maxTessEvaluationUniformComponents = 1024;
    r.maxTessPatchComponents = 120; r.maxPatchVertices = 32;
    r.maxTessGenLevel = 64; r.maxViewports = 16;
    r.maxVertexAtomicCounters = 0; r.maxTessControlAtomicCounters = 0;
    r.maxTessEvaluationAtomicCounters = 0; r.maxGeometryAtomicCounters = 0;
    r.maxFragmentAtomicCounters = 8; r.maxCombinedAtomicCounters = 8;
    r.maxAtomicCounterBindings = 1; r.maxVertexAtomicCounterBuffers = 0;
    r.maxTessControlAtomicCounterBuffers = 0;
    r.maxTessEvaluationAtomicCounterBuffers = 0;
    r.maxGeometryAtomicCounterBuffers = 0;
    r.maxFragmentAtomicCounterBuffers = 1; r.maxCombinedAtomicCounterBuffers = 1;
    r.maxAtomicCounterBufferSize = 16384; r.maxTransformFeedbackBuffers = 4;
    r.maxTransformFeedbackInterleavedComponents = 64;
    r.maxCullDistances = 8; r.maxCombinedClipAndCullDistances = 8;
    r.maxSamples = 4; r.limits.nonInductiveForLoops = 1;
    r.limits.whileLoops = 1; r.limits.doWhileLoops = 1;
    r.limits.generalUniformIndexing = 1; r.limits.generalAttributeMatrixVectorIndexing = 1;
    r.limits.generalVaryingIndexing = 1; r.limits.generalSamplerIndexing = 1;
    r.limits.generalVariableIndexing = 1; r.limits.generalConstantMatrixVectorIndexing = 1;
    return r;
}

VkShaderModule compileComputeShader(VulkanContext* ctx,
                                    const char* glslSource,
                                    const char* nameForDebug) {
    glslang::InitializeProcess();

    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&glslSource, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute,
                       glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    TBuiltInResource res = defaultResources();
    if (!shader.parse(&res, 450, false, EShMsgDefault)) {
        DEJAVISUI_LOG_ERROR("[%s] GLSL parse error:\n%s\n%s",
                            nameForDebug, shader.getInfoLog(), shader.getInfoDebugLog());
        glslang::FinalizeProcess();
        return VK_NULL_HANDLE;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        DEJAVISUI_LOG_ERROR("[%s] GLSL link error:\n%s",
                            nameForDebug, program.getInfoLog());
        glslang::FinalizeProcess();
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spv);
    glslang::FinalizeProcess();

    VkShaderModuleCreateInfo smi{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smi.codeSize = spv.size() * sizeof(uint32_t);
    smi.pCode    = spv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx->device, &smi, nullptr, &mod) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[%s] vkCreateShaderModule failed", nameForDebug);
        return VK_NULL_HANDLE;
    }
    return mod;
}

} // namespace yuvconv