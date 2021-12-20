#pragma once

#include <optional>

#include <fmt/format.h>

#include "HandleWrapper.hpp"

class PhysicalDevice : public HandleWrapper<VkPhysicalDevice> {
  public:
	PhysicalDevice() = default;
	explicit PhysicalDevice(VkPhysicalDevice handle) : HandleWrapper(handle) { init(); }

	void init(VkPhysicalDevice handle) {
		_handle = handle;
		init();
	}

	void init();

	const VkPhysicalDeviceFeatures&				  getFeatures() const { return _features; }
	const VkPhysicalDeviceProperties&			  getProperties() const { return _properties; };
	const VkPhysicalDeviceRayTracingPropertiesNV& getRaytracingPipelineProperties() const { return _rayTracingProperties; };
	const std::vector<VkQueueFamilyProperties>	  getQueueFamilies() const { return _queueFamilies; }

	struct QueueFamilyIndices {
		QueueFamilyIndices(VkSurfaceKHR surface, const PhysicalDevice& device) {
			int i = 0;

			for(const auto& queueFamily : device.getQueueFamilies()) {
				if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
					graphicsFamily = i;

				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
				if(presentSupport)
					presentFamily = i;

				i++;
			}
		}

		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;
	};

	struct SwapChainSupportDetails {
		SwapChainSupportDetails(VkSurfaceKHR surface, VkPhysicalDevice device) {
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);

			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
			if(formatCount != 0) {
				formats.resize(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, formats.data());
			}

			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

			if(presentModeCount != 0) {
				presentModes.resize(presentModeCount);
				vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, presentModes.data());
			}
		}
		VkSurfaceCapabilitiesKHR		capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR>	presentModes;
	};

	QueueFamilyIndices		getQueues(const VkSurfaceKHR& surface) const { return QueueFamilyIndices(surface, *this); }
	SwapChainSupportDetails getSwapChainSupport(const VkSurfaceKHR& surface) const { return SwapChainSupportDetails(surface, _handle); }

	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
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

  private:
	VkPhysicalDeviceFeatures			   _features;
	VkPhysicalDeviceProperties			   _properties;
	VkPhysicalDeviceMemoryProperties	   _memoryProperties;
	VkPhysicalDeviceRayTracingPropertiesNV _rayTracingProperties;

	std::vector<VkQueueFamilyProperties> _queueFamilies;
};
