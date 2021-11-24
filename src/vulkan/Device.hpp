#pragma once

#include <cassert>
#include <functional>
#include <set>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "PhysicalDevice.hpp"

class CommandPool;
class CommandBuffer;

class Device : public HandleWrapper<VkDevice> {
  public:
	Device() = default;
	Device(Device&& d) noexcept = default;
	Device& operator=(Device&& d) noexcept = default;

	explicit Device(VkSurfaceKHR surface, const PhysicalDevice& physicalDevice, const std::vector<const char*>& requiredDeviceExtensions) {
		PhysicalDevice::QueueFamilyIndices indices = physicalDevice.getQueues(surface);
		std::set<uint32_t>				   uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

		float								 queuePriority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		for(uint32_t queueFamily : uniqueQueueFamilies) {
			VkDeviceQueueCreateInfo queueCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures{.samplerAnisotropy = VK_TRUE};
		VkDeviceCreateInfo		 createInfo{
				  .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				  .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
				  .pQueueCreateInfos = queueCreateInfos.data(),
				  .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
				  .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
				  .pEnabledFeatures = &deviceFeatures,
		  };

		if(vkCreateDevice(physicalDevice, &createInfo, nullptr, &_handle) != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device!");
		}
		_physicalDevice = &physicalDevice;
	}

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

	void submit(const uint32_t queueFamilyIndex, std::function<void(const CommandBuffer&)> function) const;

  private:
	const PhysicalDevice* _physicalDevice = nullptr;
};
