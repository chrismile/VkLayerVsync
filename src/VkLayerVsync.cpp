/*
 * BSD 3-Clause License
 *
 * Copyright (c) 2026, Christoph Neuhauser
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <map>
#include <algorithm>
#include <mutex>
#include <cstring>
#include <cctype>
#include <vulkan/vk_layer.h>
#include <vulkan/utility/vk_dispatch_table.h>
#include <vulkan/layer/vk_layer_settings.h>
#include <vulkan/layer/vk_layer_settings.hpp>

#ifdef _WIN32
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

static const char* const VK_LAYER_NAME = "VK_LAYER_CHRISMILE_vsync";

/// Retrieves the dispatch table pointer to be used as the key for the instance and device maps.
template <typename DispatchT>
void* getDispatchKey(DispatchT inst) {
    return *reinterpret_cast<void**>(inst);
}

/// Layer settings. Default is that vsync is forced off. Shared between instance and device space.
struct VsyncLayerSettings {
    bool overwriteVsync = true;
    VkPresentModeKHR vsyncMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
};

// Instance and device dispatch tables and settings.
static std::map<void*, VkuInstanceDispatchTable> instanceDispatchTables;
static std::map<void*, VsyncLayerSettings> instanceLayerSettings;
static std::map<void*, VkuDeviceDispatchTable> deviceDispatchTables;
static std::map<void*, VsyncLayerSettings> deviceLayerSettings;

/**
 * Global lock for access to the maps above.
 * Theoretically, we could switch to vku::concurrent::unordered_map, but this is likely not a performance bottleneck.
 */
static std::mutex globalMutex;
typedef std::lock_guard<std::mutex> scoped_lock;


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    if (pPropertyCount) {
        *pPropertyCount = 1;
    }

    if (pProperties) {
        strcpy(pProperties->layerName, VK_LAYER_NAME);
        strcpy(pProperties->description, "Vsync layer - https://github.com/chrismile/VkLayerVsync");
        pProperties->implementationVersion = 1;
        pProperties->specVersion = VK_MAKE_API_VERSION(0, 1, 3, 234);
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_EnumerateInstanceExtensionProperties(
        const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    if (!pLayerName || strcmp(pLayerName, VK_LAYER_NAME) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pPropertyCount) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_EnumerateDeviceLayerProperties(
        VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    return VsyncLayer_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_EnumerateDeviceExtensionProperties(
        VkPhysicalDevice physicalDevice, const char* pLayerName,
        uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    if (!pLayerName || strcmp(pLayerName, VK_LAYER_NAME) != 0) {
        if (physicalDevice == VK_NULL_HANDLE) {
            return VK_SUCCESS;
        }

        scoped_lock l(globalMutex);
        return instanceDispatchTables[getDispatchKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    if (pPropertyCount) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_CreateInstance(
        const VkInstanceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkInstance* pInstance) {
    auto* createInfoChain = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (createInfoChain && (createInfoChain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
            || createInfoChain->function != VK_LAYER_LINK_INFO)) {
        createInfoChain = (VkLayerInstanceCreateInfo*)createInfoChain->pNext;
    }
    if (!createInfoChain || !createInfoChain->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto pGetInstanceProcAddr = createInfoChain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto pCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(pGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (!pCreateInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the chain for passing to the next layer and create the instance.
    createInfoChain->u.pLayerInfo = createInfoChain->u.pLayerInfo->pNext;
    VkResult result = pCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Populate the dispatch table using the functions of the next layer.
    VkuInstanceDispatchTable dispatchTable;
    dispatchTable.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)pGetInstanceProcAddr(*pInstance, "vkGetInstanceProcAddr");
    dispatchTable.DestroyInstance = (PFN_vkDestroyInstance)pGetInstanceProcAddr(*pInstance, "vkDestroyInstance");
    dispatchTable.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)pGetInstanceProcAddr(
            *pInstance, "vkEnumerateDeviceExtensionProperties");

    /*
     * Parse the layer settings. For more information how to pass settings, please refer to README.md or:
     * - https://vulkan.lunarg.com/doc/view/latest/windows/layer_configuration.html
     * - https://github.com/KhronosGroup/Vulkan-Utility-Libraries/blob/main/docs/layer_configuration.md#layer-settings-environment-variables
     * - https://github.com/KhronosGroup/Vulkan-Utility-Libraries/blob/main/docs/layer_configuration.md#configuring-the-layer-settings-using-vk_ext_layer_settings
     */
    VkuLayerSettingSet layerSettingSet = VK_NULL_HANDLE;
    result = vkuCreateLayerSettingSet(
            VK_LAYER_NAME, vkuFindLayerSettingsCreateInfo(pCreateInfo), pAllocator, nullptr, &layerSettingSet);
    if (result != VK_SUCCESS) {
        return result;
    }
    VsyncLayerSettings settings{};
    const char *settingKeyVsync = "vsync";
    if (vkuHasLayerSetting(layerSettingSet, settingKeyVsync)) {
        std::string vsyncModeString;
        vkuGetLayerSettingValue(layerSettingSet, settingKeyVsync, vsyncModeString);
        std::transform(
                vsyncModeString.begin(), vsyncModeString.end(), vsyncModeString.begin(),
                [](unsigned char c){ return std::tolower(c); });
        if (vsyncModeString == "off" || vsyncModeString == "0" || vsyncModeString == "immediate") {
            settings.overwriteVsync = true;
            settings.vsyncMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else if (vsyncModeString == "on" || vsyncModeString == "1" || vsyncModeString == "fifo") {
            settings.overwriteVsync = true;
            settings.vsyncMode = VK_PRESENT_MODE_FIFO_KHR;
        } else if (vsyncModeString == "fifo_relaxed") {
            settings.overwriteVsync = true;
            settings.vsyncMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        } else if (vsyncModeString == "mailbox") {
            settings.overwriteVsync = true;
            settings.vsyncMode = VK_PRESENT_MODE_MAILBOX_KHR;
        } else if (vsyncModeString == "passthrough") {
            settings.overwriteVsync = false;
        }
    }
    vkuDestroyLayerSettingSet(layerSettingSet, pAllocator);

    {
        scoped_lock l(globalMutex);
        instanceDispatchTables[getDispatchKey(*pInstance)] = dispatchTable;
        instanceLayerSettings[getDispatchKey(*pInstance)] = settings;
    }
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL VsyncLayer_DestroyInstance(
        VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    scoped_lock l(globalMutex);
    auto pDestroyInstance = instanceDispatchTables[getDispatchKey(instance)].DestroyInstance;
    instanceDispatchTables.erase(getDispatchKey(instance));
    instanceLayerSettings.erase(getDispatchKey(instance));
    pDestroyInstance(instance, pAllocator);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_CreateDevice(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice) {
    auto* createInfoChain = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (createInfoChain && (createInfoChain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
            || createInfoChain->function != VK_LAYER_LINK_INFO)) {
        createInfoChain = (VkLayerDeviceCreateInfo*)createInfoChain->pNext;
    }

    if (!createInfoChain || !createInfoChain->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr pGetInstanceProcAddr = createInfoChain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pGetDeviceProcAddr = createInfoChain->u.pLayerInfo->pfnNextGetDeviceProcAddr;

    // Advance the chain for passing to the next layer and create the device.
    createInfoChain->u.pLayerInfo = createInfoChain->u.pLayerInfo->pNext;
    auto pCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(pGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice"));
    VkResult result = pCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Populate the dispatch table using the functions of the next layer.
    VkuDeviceDispatchTable dispatchTable;
    dispatchTable.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)pGetDeviceProcAddr(*pDevice, "vkGetDeviceProcAddr");
    dispatchTable.DestroyDevice = (PFN_vkDestroyDevice)pGetDeviceProcAddr(*pDevice, "vkDestroyDevice");
    dispatchTable.CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)pGetDeviceProcAddr(*pDevice, "vkCreateSwapchainKHR");

    {
        scoped_lock l(globalMutex);
        deviceDispatchTables[getDispatchKey(*pDevice)] = dispatchTable;
        deviceLayerSettings[getDispatchKey(*pDevice)] = instanceLayerSettings[getDispatchKey(physicalDevice)];
    }
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR void VKAPI_CALL VsyncLayer_DestroyDevice(
        VkDevice device, const VkAllocationCallbacks* pAllocator) {
    scoped_lock l(globalMutex);
    auto pDestroyDevice = deviceDispatchTables[getDispatchKey(device)].DestroyDevice;
    deviceDispatchTables.erase(getDispatchKey(device));
    deviceLayerSettings.erase(getDispatchKey(device));
    pDestroyDevice(device, pAllocator);
}


VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL VsyncLayer_CreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain) {
    scoped_lock l(globalMutex);

    const auto& settings = deviceLayerSettings[getDispatchKey(device)];
    VkSwapchainCreateInfoKHR createInfoCopy = *pCreateInfo;
    if (settings.overwriteVsync) {
        createInfoCopy.presentMode = settings.vsyncMode;
    }
    return deviceDispatchTables[getDispatchKey(device)].CreateSwapchainKHR(device, &createInfoCopy, pAllocator, pSwapchain);
}


#define GETPROCADDR_VK(func) if (strcmp(pName, "vk" #func) == 0) { return (PFN_vkVoidFunction)&VsyncLayer_##func; }

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL VsyncLayer_GetDeviceProcAddr(VkDevice device, const char* pName) {
    GETPROCADDR_VK(GetDeviceProcAddr);
    GETPROCADDR_VK(EnumerateDeviceLayerProperties);
    GETPROCADDR_VK(EnumerateDeviceExtensionProperties);
    GETPROCADDR_VK(CreateDevice);
    GETPROCADDR_VK(DestroyDevice);
    GETPROCADDR_VK(CreateSwapchainKHR);

    {
        scoped_lock l(globalMutex);
        return deviceDispatchTables[getDispatchKey(device)].GetDeviceProcAddr(device, pName);
    }
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL VsyncLayer_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    GETPROCADDR_VK(GetInstanceProcAddr);
    GETPROCADDR_VK(EnumerateInstanceLayerProperties);
    GETPROCADDR_VK(EnumerateInstanceExtensionProperties);
    GETPROCADDR_VK(CreateInstance);
    GETPROCADDR_VK(DestroyInstance);

    GETPROCADDR_VK(GetDeviceProcAddr);
    GETPROCADDR_VK(EnumerateDeviceLayerProperties);
    GETPROCADDR_VK(EnumerateDeviceExtensionProperties);
    GETPROCADDR_VK(CreateDevice);
    GETPROCADDR_VK(DestroyDevice);
    GETPROCADDR_VK(CreateSwapchainKHR);

    {
        scoped_lock l(globalMutex);
        return instanceDispatchTables[getDispatchKey(instance)].GetInstanceProcAddr(instance, pName);
    }
}
