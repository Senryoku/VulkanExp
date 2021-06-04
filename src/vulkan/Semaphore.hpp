#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class Semaphore : public HandleWrapper<VkSemaphore> {
  public:
    Semaphore() = default;
    explicit Semaphore(VkDevice device) {
        create(device);
    }

    void create(VkDevice device) {
        VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if(vkCreateSemaphore(device, &info, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphores!");
        }
        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroySemaphore(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~Semaphore() {
        destroy();
    }

  private:
    VkDevice _device = VK_NULL_HANDLE;
};