#include "PhysicalDevice.hpp"

void PhysicalDevice::init() {
	vkGetPhysicalDeviceFeatures(_handle, &_features);

	_rayTracingPipelineProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2 tempProperties2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &_rayTracingPipelineProperties,
	};
	vkGetPhysicalDeviceProperties2(_handle, &tempProperties2);

	_properties = tempProperties2.properties;
	vkGetPhysicalDeviceMemoryProperties(_handle, &_memoryProperties);


	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(_handle, &queueFamilyCount, nullptr);
	_queueFamilies.resize(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(_handle, &queueFamilyCount, _queueFamilies.data());

	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(_handle, nullptr, &extensionCount, nullptr);
	_extensions.resize(extensionCount);
	vkEnumerateDeviceExtensionProperties(_handle, nullptr, &extensionCount, _extensions.data());
}

PhysicalDevice::QueueFamilyIndex PhysicalDevice::getGraphicsQueueFamilyIndex() const {
	QueueFamilyIndex bestFit = -1;
	for(QueueFamilyIndex i = 0; i < _queueFamilies.size(); ++i) {
		if(_queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			if(!(_queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
				return i;
			bestFit = i;
		}
	}
	return bestFit;
}

PhysicalDevice::QueueFamilyIndex PhysicalDevice::getComputeQueueFamilyIndex() const {
	QueueFamilyIndex bestFit = -1;
	for(QueueFamilyIndex i = 0; i < _queueFamilies.size(); ++i) {
		if(_queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			if(!(_queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				return i;
			bestFit = i;
		}
	}
	return bestFit;
}

PhysicalDevice::QueueFamilyIndex PhysicalDevice::getTransfertQueueFamilyIndex() const {
	QueueFamilyIndex bestFit = -1;
	for(QueueFamilyIndex i = 0; i < _queueFamilies.size(); ++i) {
		if(_queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
			// Return immediatly if this is a dedicated transfert queue
			if(!((_queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || (_queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)))
				return i;
			bestFit = i;
		}
	}
	// Queues don't have to explicitly report VK_QUEUE_TRANSFER_BIT, but any GRAPHICS or COMPUTE queue actually supports transfert operation, return one of those if we didn't
	// find one explicitly supporting TRANSFERT
	if(bestFit == -1)
		for(QueueFamilyIndex i = 0; i < _queueFamilies.size(); ++i)
			if((_queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || (_queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
				return i;
	return bestFit;
}

PhysicalDevice::QueueFamilyIndex PhysicalDevice::getPresentQueueFamilyIndex(VkSurfaceKHR surface) const {
	for(QueueFamilyIndex i = 0; i < _queueFamilies.size(); ++i) {
		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(_handle, i, surface, &presentSupport);
		if(presentSupport)
			return i;
	}
	return -1;
}

uint32_t PhysicalDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
	for(uint32_t i = 0; i < _memoryProperties.memoryTypeCount; i++) {
		if((typeFilter & (1 << i)) && (_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	// Fallback
	for(uint32_t i = 0; i < _memoryProperties.memoryTypeCount; i++) {
		if((typeFilter & (1 << i)) && (_memoryProperties.memoryTypes[i].propertyFlags & properties)) {
			return i;
		}
	}

	throw std::runtime_error(fmt::format("Failed to find suitable memory type ({} {}).", typeFilter, properties));
}

VkFormat PhysicalDevice::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for(VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(_handle, format, &props);

		if(tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		} else if(tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("Failed to find supported format.");
}
