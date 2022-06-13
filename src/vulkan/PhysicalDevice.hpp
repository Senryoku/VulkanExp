#pragma once

#include <optional>
#include <vector>

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

	inline const std::vector<VkExtensionProperties>&	 getExtensions() const { return _extensions; }
	inline const VkPhysicalDeviceFeatures&				 getFeatures() const { return _features; }
	inline const VkPhysicalDeviceProperties&			 getProperties() const { return _properties; };
	inline const VkPhysicalDeviceRayTracingPropertiesNV& getRaytracingPipelineProperties() const { return _rayTracingProperties; };
	inline const std::vector<VkQueueFamilyProperties>	 getQueueFamilies() const { return _queueFamilies; }

	using QueueFamilyIndex = uint32_t;

	QueueFamilyIndex getGraphicsQueueFamilyIndex() const;
	QueueFamilyIndex getComputeQueueFamilyIndex() const;
	QueueFamilyIndex getTransfertQueueFamilyIndex() const;
	QueueFamilyIndex getPresentQueueFamilyIndex(VkSurfaceKHR surface) const;

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

	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
	VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

  private:
	std::vector<VkExtensionProperties>	   _extensions;
	VkPhysicalDeviceFeatures			   _features{};
	VkPhysicalDeviceProperties			   _properties{};
	VkPhysicalDeviceMemoryProperties	   _memoryProperties{};
	VkPhysicalDeviceRayTracingPropertiesNV _rayTracingProperties{};

	std::vector<VkQueueFamilyProperties> _queueFamilies;
};
