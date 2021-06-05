#pragma once

#include <stdexcept>

#include "../Logger.hpp"
#include "CommandPool.hpp"
#include "HandleWrapper.hpp"

class Buffer : public HandleWrapper<VkBuffer> {
  public:
    Buffer() = default;
    void create(VkDevice device, VkBufferUsageFlags usage, size_t size) {
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if(vkCreateBuffer(device, &bufferInfo, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create vertex buffer!");
        }

        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyBuffer(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~Buffer() {
        destroy();
    }

    VkMemoryRequirements getMemoryRequirements() const {
        if(!isValid()) {
            warn("Buffer::Call to 'getMemoryRequirements' on an invalid Buffer.");
            return VkMemoryRequirements{};
        }
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(_device, _handle, &memRequirements);
        return memRequirements;
    }

    void copyFromStagingBuffer(const CommandPool& tmpCommandPool, const Buffer& stagingBuffer, size_t size, VkQueue queue) const;

  private:
    VkDevice _device = VK_NULL_HANDLE;
};
