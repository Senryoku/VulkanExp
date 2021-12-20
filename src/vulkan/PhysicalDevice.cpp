#include "PhysicalDevice.hpp"

void PhysicalDevice::init() {
	vkGetPhysicalDeviceFeatures(_handle, &_features);

	_rayTracingProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2 tempProperties2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &_rayTracingProperties,
	};
	vkGetPhysicalDeviceProperties2(_handle, &tempProperties2);
	_properties = tempProperties2.properties;
	vkGetPhysicalDeviceMemoryProperties(_handle, &_memoryProperties);

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(_handle, &queueFamilyCount, nullptr);
	_queueFamilies.resize(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(_handle, &queueFamilyCount, _queueFamilies.data());
}
