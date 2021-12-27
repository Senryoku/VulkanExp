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

		VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &_handle));

		_device = device;
	}

	void reset(VkCommandPoolResetFlags flags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT) { vkResetCommandPool(_device, _handle, flags); }

	void destroy() {
		if(isValid()) {
			vkDestroyCommandPool(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~CommandPool() { destroy(); }

  private:
	VkDevice _device;
};
