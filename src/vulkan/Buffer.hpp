#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

template <int BufferType> class Buffer : public HandleWrapper<VkBuffer> {
  public:
    void create(VkDevice device, size_t size) {
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = BufferType,
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
        VkMemoryRequirements memRequirements;
        if(isValid())
            vkGetBufferMemoryRequirements(_device, _handle, &memRequirements);
        return memRequirements;
    }

  private:
    VkDevice _device = VK_NULL_HANDLE;
};

using VertexBuffer = Buffer<VK_BUFFER_USAGE_VERTEX_BUFFER_BIT>;