#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class Fence : public HandleWrapper<VkFence> {
  public:
    Fence() = default;
    explicit Fence(VkDevice device) {
        create(device);
    }

    void create(VkDevice device) {
        VkFenceCreateInfo info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        if(vkCreateFence(device, &info, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphores!");
        }
        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyFence(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~Fence() {
        destroy();
    }

  private:
    VkDevice _device = VK_NULL_HANDLE;
};