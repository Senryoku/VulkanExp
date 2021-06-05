#pragma once

#include "HandleWrapper.hpp"
#include <stdexcept>

class CommandPool : public HandleWrapper<VkCommandPool> {
  public:
    void create(VkDevice device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0) {
        VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = flags,
            .queueFamilyIndex = queueFamilyIndex,
        };

        if(vkCreateCommandPool(device, &poolInfo, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool!");
        }

        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyCommandPool(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~CommandPool() {
        destroy();
    }

  private:
    VkDevice _device;
};