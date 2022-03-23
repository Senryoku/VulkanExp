#pragma once

#include <cassert>
#include <functional>
#include <set>

#include "PhysicalDevice.hpp"

class CommandPool;
class CommandBuffer;
class Buffer;

class Device : public HandleWrapper<VkDevice> {
  public:
	Device() = default;
	Device(const Device&) = delete;
	Device(Device&& d) noexcept = default;
	Device& operator=(Device&& d) noexcept = default;

	Device(VkSurfaceKHR surface, const PhysicalDevice& physicalDevice, const std::vector<VkDeviceQueueCreateInfo>& queues,
		   const std::vector<const char*>& requiredDeviceExtensions);

	void destroy() {
		if(isValid()) {
			vkDestroyDevice(_handle, nullptr);
			_handle = VK_NULL_HANDLE;
			_physicalDevice = nullptr;
		}
	}

	~Device() { destroy(); }

	const PhysicalDevice& getPhysicalDevice() const {
		assert(_physicalDevice);
		return *_physicalDevice;
	}

	void submit(PhysicalDevice::QueueFamilyIndex queueFamilyIndex, std::function<void(const CommandBuffer&)>&& function) const;

	VkQueue getQueue(PhysicalDevice::QueueFamilyIndex family, uint32_t queueIndex = 0) const {
		VkQueue queue;
		vkGetDeviceQueue(_handle, family, 0, &queue);
		return queue;
	}

	inline void immediateSubmitTransfert(std::function<void(const CommandBuffer&)>&& func) const {
		submit(getPhysicalDevice().getTransfertQueueFamilyIndex(), std::forward<std::function<void(const CommandBuffer&)>>(func));
	}
	inline void immediateSubmitGraphics(std::function<void(const CommandBuffer&)>&& func) const {
		submit(getPhysicalDevice().getGraphicsQueueFamilyIndex(), std::forward<std::function<void(const CommandBuffer&)>>(func));
	}
	inline void immediateSubmitCompute(std::function<void(const CommandBuffer&)>&& func) const {
		submit(getPhysicalDevice().getComputeQueueFamilyIndex(), std::forward<std::function<void(const CommandBuffer&)>>(func));
	}
	
  private:
	const PhysicalDevice* _physicalDevice = nullptr;
};
