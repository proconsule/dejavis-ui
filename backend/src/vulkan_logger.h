#pragma once
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>

class VulkanLog {
public:
    static inline PFN_vkCmdBeginDebugUtilsLabelEXT pfnBeginLabel = nullptr;
    static inline PFN_vkCmdEndDebugUtilsLabelEXT pfnEndLabel = nullptr;
    static inline PFN_vkSetDebugUtilsObjectNameEXT pfnSetObjectName = nullptr;
    static inline VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    static inline std::string loggername = "";

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pData,
        void* pUserData) {

        std::string prefix;
        std::string color;
        std::string myendl;
        std::string resetcolor="\0";

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            prefix = "[Vulkan ERROR] ";
            color = "\033[1;31m";
            myendl = "\n\n";
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            prefix = "[Vulkan WARNING] ";
            color = "\033[1;33m";
            myendl = "\n\n";
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            prefix = "[Vulkan INFO] ";
            color = "\033[1;36m";
            myendl = "\n";
        } else {
            prefix = "[Vulkan VERBOSE] ";
            color = "\033[0;90m";
            myendl = "\n\n";
        }

        std::cout <<"[" << loggername << "] " << color << prefix << "\033[0m" << pData->pMessage << myendl;
        return VK_FALSE;
    }

    static void Init(VkInstance instance, VkDevice device,std::string _loggername) {
        loggername = _loggername;

        pfnBeginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
        pfnEndLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");
        pfnSetObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");

        auto pfnCreateMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

        if (pfnCreateMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            createInfo.pfnUserCallback = DebugCallback;

            pfnCreateMessenger(instance, &createInfo, nullptr, &debugMessenger);
            std::cout << "VulkanLog: Sistema di monitoraggio ATTIVO." << std::endl;
        } else {
            std::cout << "VulkanLog: Impossibile attivare il Messenger (Estensione mancante?)" << std::endl;
        }
    }

    static void BeginRegion(VkCommandBuffer cmd, const char* name) {
        if (pfnBeginLabel) {
            VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
            label.pLabelName = name;
            pfnBeginLabel(cmd, &label);
        }
    }

    static void EndRegion(VkCommandBuffer cmd) {
        if (pfnEndLabel) pfnEndLabel(cmd);
    }

    static void LogCustom(VkInstance instance, const char* message) {
        auto pfnSubmitMessage = (PFN_vkSubmitDebugUtilsMessageEXT)vkGetInstanceProcAddr(instance, "vkSubmitDebugUtilsMessageEXT");
        if (pfnSubmitMessage) {
            VkDebugUtilsMessengerCallbackDataEXT data = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT };
            data.pMessage = message;

            // Lo inviamo come INFO così lo vedi nel terminale
            pfnSubmitMessage(instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data);
        }
    }

    static void Shutdown(VkInstance instance) {
        auto pfnDestroyMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (pfnDestroyMessenger && debugMessenger != VK_NULL_HANDLE) {
            pfnDestroyMessenger(instance, debugMessenger, nullptr);
        }
    }
};