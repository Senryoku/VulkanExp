#pragma once

#include <cassert>
#include <functional>
#include <set>

#include "PhysicalDevice.hpp"

class CommandPool;
class CommandBuffer;

class Device : public HandleWrapper<VkDevice> {
  public:
	Device() = default;
	Device(const Device&) = delete;
	Device(Device&& d) noexcept = default;
	Device& operator=(Device&& d) noexcept = default;

	explicit Device(VkSurfaceKHR surface, const PhysicalDevice& physicalDevice, const std::vector<VkDeviceQueueCreateInfo>& queues,
					const std::vector<const char*>& requiredDeviceExtensions) {
		VkPhysicalDeviceFeatures deviceFeatures{
			.samplerAnisotropy = VK_TRUE,
		};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceFeatureRayTracingPipeline{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
			.pNext = VK_NULL_HANDLE,
			.rayTracingPipeline = VK_TRUE,
		};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceFeaturesAccStruct{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
			.pNext = &deviceFeatureRayTracingPipeline,
			.accelerationStructure = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features deviceFeatures12{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &deviceFeaturesAccStruct,
			.descriptorIndexing = VK_TRUE,
			.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
			.runtimeDescriptorArray = VK_TRUE,
			.bufferDeviceAddress = VK_TRUE,
		};
		VkDeviceCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &deviceFeatures12,
			.queueCreateInfoCount = static_cast<uint32_t>(queues.size()),
			.pQueueCreateInfos = queues.data(),
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
			.ppEnabledExtensionNames = requiredDeviceExtensions.data(),
			.pEnabledFeatures = &deviceFeatures,
		};

		VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, nullptr, &_handle));
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

	VkQueue getQueue(PhysicalDevice::QueueFamilyIndex family, uint32_t queueIndex = 0) const {
		VkQueue queue;
		vkGetDeviceQueue(_handle, family, 0, &queue);
		return queue;
	}

  private:
	const PhysicalDevice* _physicalDevice = nullptr;
};
