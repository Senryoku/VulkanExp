#include "Device.hpp"

#include "CommandBuffer.hpp"
#include "CommandPool.hpp"

Device::Device(VkSurfaceKHR surface, const PhysicalDevice& physicalDevice, const std::vector<VkDeviceQueueCreateInfo>& queues,
			   const std::vector<const char*>& requiredDeviceExtensions) {
	// FIXME: This shouldn't be baked in this class.
	VkPhysicalDeviceFeatures deviceFeatures{
		.samplerAnisotropy = VK_TRUE,
	};
	VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
		.pNext = nullptr,
		.shaderSharedFloat32Atomics = true,
		.shaderSharedFloat32AtomicAdd = true,
	};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceFeatureRayTracingPipeline{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
		.pNext = &atomicFloatFeatures,
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

void Device::submit(PhysicalDevice::QueueFamilyIndex queueFamilyIndex, std::function<void(const CommandBuffer&)>&& function) const {
	CommandPool stagingCommandPool;
	stagingCommandPool.create(_handle, queueFamilyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	CommandBuffers stagingCommands;
	stagingCommands.allocate(_handle, stagingCommandPool, 1);
	stagingCommands[0].begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	function(stagingCommands[0]);

	stagingCommands[0].end();
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = stagingCommands.getBuffersHandles().data(),
	};
	auto queue = getQueue(queueFamilyIndex);
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(queue));
}
