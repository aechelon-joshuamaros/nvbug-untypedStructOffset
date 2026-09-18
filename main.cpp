#define USE_VALIDATION_LAYERS
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
};

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    std::optional<uint32_t> memoryTypeIndex = std::nullopt;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((memoryTypeBits & (1u << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memoryTypeIndex = i;
            break;
        }
    }
    assert(memoryTypeIndex.has_value());
    return *memoryTypeIndex;
}

Buffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage) {
    const VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buffer;
    assert(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer) == VK_SUCCESS);

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);
    const VkMemoryAllocateFlagsInfo allocateFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    const VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &allocateFlagsInfo,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VkDeviceMemory memory;
    assert(vkAllocateMemory(device, &allocateInfo, nullptr, &memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS);
    return { buffer, memory, size };
}

VkDeviceAddress getBufferAddress(VkDevice device, VkBuffer buffer) {
    const VkBufferDeviceAddressInfo addressInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vkGetBufferDeviceAddress(device, &addressInfo);
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

int main() {
    const std::vector<const char*> layers = {
#ifdef USE_VALIDATION_LAYERS
        "VK_LAYER_KHRONOS_validation",
#endif
    };
    const VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "reproStructOffset",
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = uint32_t(layers.size()),
        .ppEnabledLayerNames = layers.data(),
    };
    VkInstance instance;
    assert(vkCreateInstance(&instanceCreateInfo, nullptr, &instance) == VK_SUCCESS);
    const PFN_vkGetPhysicalDeviceDescriptorSizeEXT getPhysicalDeviceDescriptorSize = reinterpret_cast<PFN_vkGetPhysicalDeviceDescriptorSizeEXT>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceDescriptorSizeEXT"));
    const PFN_vkWriteResourceDescriptorsEXT writeResourceDescriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(vkGetInstanceProcAddr(instance, "vkWriteResourceDescriptorsEXT"));
    const PFN_vkCmdBindResourceHeapEXT cmdBindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(vkGetInstanceProcAddr(instance, "vkCmdBindResourceHeapEXT"));
    assert(getPhysicalDeviceDescriptorSize != nullptr);
    assert(writeResourceDescriptors != nullptr);
    assert(cmdBindResourceHeap != nullptr);

    uint32_t physicalDeviceCount;
    assert(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr) == VK_SUCCESS);
    assert(physicalDeviceCount > 0);
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    assert(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()) == VK_SUCCESS);
    std::optional<VkPhysicalDevice> physicalDevice = std::nullopt;
    std::optional<uint32_t> queueFamilyIndex = std::nullopt;
    for (const VkPhysicalDevice candidate : physicalDevices) {
        uint32_t extensionCount;
        assert(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr) == VK_SUCCESS);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        assert(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, availableExtensions.data()) == VK_SUCCESS);
        bool supportsDescriptorHeap = false;
        bool supportsUntypedPointers = false;
        bool supportsMaintenance5 = false;
        for (const VkExtensionProperties& extension : availableExtensions) {
            if (std::strcmp(extension.extensionName, VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME) == 0) {
                supportsDescriptorHeap = true;
            }
            if (std::strcmp(extension.extensionName, VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME) == 0) {
                supportsUntypedPointers = true;
            }
            if (std::strcmp(extension.extensionName, VK_KHR_MAINTENANCE_5_EXTENSION_NAME) == 0) {
                supportsMaintenance5 = true;
            }
        }
        if (!supportsDescriptorHeap || !supportsUntypedPointers || !supportsMaintenance5) {
            continue;
        }
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());
        VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptorHeapFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
        };
        VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedPointersFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
            .pNext = &descriptorHeapFeatures,
        };
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
            .pNext = &untypedPointersFeatures,
        };
        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &bufferDeviceAddressFeatures,
        };
        vkGetPhysicalDeviceFeatures2(candidate, &features2);
        if (descriptorHeapFeatures.descriptorHeap == VK_TRUE && untypedPointersFeatures.shaderUntypedPointers == VK_TRUE && bufferDeviceAddressFeatures.bufferDeviceAddress == VK_TRUE) {
            for (uint32_t i = 0; i < queueFamilyCount; ++i) {
                if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
                    physicalDevice = candidate;
                    queueFamilyIndex = i;
                    break;
                }
            }
        }
        if (physicalDevice.has_value()) {
            break;
        }
    }
    assert(physicalDevice.has_value());
    assert(queueFamilyIndex.has_value());

    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = *queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptorHeapFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
        .descriptorHeap = VK_TRUE,
    };
    VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedPointersFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
        .pNext = &descriptorHeapFeatures,
        .shaderUntypedPointers = VK_TRUE,
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &untypedPointersFeatures,
        .bufferDeviceAddress = VK_TRUE,
    };
    const std::array<const char*, 3> extensions = {
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
        VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &bufferDeviceAddressFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = uint32_t(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    VkDevice device;
    assert(vkCreateDevice(*physicalDevice, &deviceCreateInfo, nullptr, &device) == VK_SUCCESS);
    VkQueue queue;
    vkGetDeviceQueue(device, *queueFamilyIndex, 0, &queue);

    int32_t countingUp[16];
    for (int32_t i = 0; i < sizeof(countingUp) / sizeof(int32_t); i++) {
        countingUp[i] = i;
    }
    const VkBufferUsageFlags uniformUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const Buffer flatBuffer = createBuffer(device, *physicalDevice, sizeof(countingUp), uniformUsage);
    const Buffer nestedBuffer = createBuffer(device, *physicalDevice, sizeof(countingUp), uniformUsage);
    const Buffer outputBuffer = createBuffer(device, *physicalDevice, sizeof(countingUp) * 2, storageUsage);
    void* mappedMemory;
    assert(vkMapMemory(device, flatBuffer.memory, 0, flatBuffer.size, 0, &mappedMemory) == VK_SUCCESS);
    std::memcpy(mappedMemory, countingUp, sizeof(countingUp));
    vkUnmapMemory(device, flatBuffer.memory);
    assert(vkMapMemory(device, nestedBuffer.memory, 0, nestedBuffer.size, 0, &mappedMemory) == VK_SUCCESS);
    std::memcpy(mappedMemory, countingUp, sizeof(countingUp));
    vkUnmapMemory(device, nestedBuffer.memory);

    VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptorHeapProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &descriptorHeapProperties,
    };
    vkGetPhysicalDeviceProperties2(*physicalDevice, &properties2);
    const VkDeviceSize descriptorSize = getPhysicalDeviceDescriptorSize(*physicalDevice, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const VkDeviceSize descriptorStride = alignUp(descriptorSize, descriptorHeapProperties.bufferDescriptorAlignment);
    const VkDeviceSize reservedRangeAlignment = std::max(descriptorHeapProperties.bufferDescriptorAlignment, descriptorHeapProperties.imageDescriptorAlignment);
    const VkDeviceSize reservedRangeOffset = alignUp(descriptorStride * 3, reservedRangeAlignment);
    const VkDeviceSize heapSize = reservedRangeOffset + descriptorHeapProperties.minResourceHeapReservedRange;
    const Buffer heapBuffer = createBuffer(device, *physicalDevice, heapSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT);
    const VkDeviceAddress heapAddress = getBufferAddress(device, heapBuffer.buffer);
    assert(heapAddress % descriptorHeapProperties.resourceHeapAlignment == 0);

    const std::array<VkDeviceAddressRangeEXT, 3> addressRanges = {{
        { getBufferAddress(device, flatBuffer.buffer), flatBuffer.size },
        { getBufferAddress(device, nestedBuffer.buffer), nestedBuffer.size },
        { getBufferAddress(device, outputBuffer.buffer), outputBuffer.size },
    }};
    const std::array<VkResourceDescriptorInfoEXT, 3> resources = {{
        { .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .data = { .pAddressRange = &addressRanges[0] } },
        { .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .data = { .pAddressRange = &addressRanges[1] } },
        { .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .data = { .pAddressRange = &addressRanges[2] } },
    }};
    assert(vkMapMemory(device, heapBuffer.memory, 0, heapBuffer.size, 0, &mappedMemory) == VK_SUCCESS);
    const std::array<VkHostAddressRangeEXT, 3> descriptorRanges = {{
        { static_cast<uint8_t*>(mappedMemory) + descriptorStride * 0, size_t(descriptorSize) },
        { static_cast<uint8_t*>(mappedMemory) + descriptorStride * 1, size_t(descriptorSize) },
        { static_cast<uint8_t*>(mappedMemory) + descriptorStride * 2, size_t(descriptorSize) },
    }};
    assert(writeResourceDescriptors(device, uint32_t(resources.size()), resources.data(), descriptorRanges.data()) == VK_SUCCESS);
    vkUnmapMemory(device, heapBuffer.memory);

    std::ifstream shaderFile("repro.spv", std::ios::binary | std::ios::ate);
    assert(shaderFile.is_open());
    const std::streamsize shaderSize = shaderFile.tellg();
    assert(shaderSize > 0 && shaderSize % 4 == 0);
    std::vector<uint32_t> shaderCode(size_t(shaderSize) / sizeof(uint32_t));
    shaderFile.seekg(0);
    shaderFile.read(reinterpret_cast<char*>(shaderCode.data()), shaderSize);
    assert(shaderFile.good());
    const VkShaderModuleCreateInfo shaderModuleCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size_t(shaderSize),
        .pCode = shaderCode.data(),
    };
    VkShaderModule shaderModule;
    assert(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main",
    };
    const VkPipelineCreateFlags2CreateInfo pipelineFlagsCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };
    const VkComputePipelineCreateInfo pipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = &pipelineFlagsCreateInfo,
        .stage = shaderStageCreateInfo,
    };
    VkPipeline pipeline;
    assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) == VK_SUCCESS);

    const VkCommandPoolCreateInfo commandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = *queueFamilyIndex,
    };
    VkCommandPool commandPool;
    assert(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool) == VK_SUCCESS);
    const VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    assert(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer) == VK_SUCCESS);
    const VkCommandBufferBeginInfo commandBufferBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    assert(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) == VK_SUCCESS);
    const VkBindHeapInfoEXT heapBindInfo = {
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .heapRange = { heapAddress, heapSize },
        .reservedRangeOffset = reservedRangeOffset,
        .reservedRangeSize = descriptorHeapProperties.minResourceHeapReservedRange,
    };
    cmdBindResourceHeap(commandBuffer, &heapBindInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    assert(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS);
    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    assert(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS);
    assert(vkDeviceWaitIdle(device) == VK_SUCCESS);

    assert(vkMapMemory(device, outputBuffer.memory, 0, outputBuffer.size, 0, &mappedMemory) == VK_SUCCESS);
    const int32_t* output = static_cast<const int32_t*>(mappedMemory);
    for (uint32_t i = 0; i < 2 * sizeof(countingUp) / sizeof(int32_t); ++i) {
        if (i == 0) {
            std::cout << "Flat struct:" << std::endl;
        }
        if (i == sizeof(countingUp) / sizeof(int32_t)) {
            std::cout << std::endl << "Nested struct:" << std::endl;
        }
        std::cout << output[i] << " ";
    }
    std::cout << std::endl;
    vkUnmapMemory(device, outputBuffer.memory);

    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    vkDestroyBuffer(device, heapBuffer.buffer, nullptr);
    vkFreeMemory(device, heapBuffer.memory, nullptr);
    vkDestroyBuffer(device, outputBuffer.buffer, nullptr);
    vkFreeMemory(device, outputBuffer.memory, nullptr);
    vkDestroyBuffer(device, nestedBuffer.buffer, nullptr);
    vkFreeMemory(device, nestedBuffer.memory, nullptr);
    vkDestroyBuffer(device, flatBuffer.buffer, nullptr);
    vkFreeMemory(device, flatBuffer.memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
