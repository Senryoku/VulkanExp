#include "PhysicalDevice.hpp"

void PhysicalDevice::init() {
	_rayTracingProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	_deviceProperties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &_rayTracingProperties,
	};
	vkGetPhysicalDeviceProperties2(_handle, &_deviceProperties);
	vkGetPhysicalDeviceMemoryProperties(_handle, &_memoryProperties);
}
