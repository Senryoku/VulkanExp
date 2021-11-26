#pragma once

#include <cassert>

#include "Device.hpp"
#include "HandleWrapper.hpp"

class Sampler : public HandleWrapper<VkSampler> {
  public:
	Sampler() = default;
	Sampler(const Device& device, const VkSamplerCreateInfo& info) : _device(&device) { create(info); }
	Sampler(const Device& device, VkFilter magFilter = VK_FILTER_LINEAR, VkFilter minFilter = VK_FILTER_LINEAR, VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT)
		: _device(&device) {
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(getDevice().getPhysicalDevice(), &properties);
		VkSamplerCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = magFilter,
			.minFilter = minFilter,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = addressModeU,
			.addressModeV = addressModeV,
			.addressModeW = addressModeW,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		create(info);
	}

	void create(const Device& device, const VkSamplerCreateInfo& info) {
		_device = &device;
		create(info);
	}

	void create(const VkSamplerCreateInfo& info) {
		if(vkCreateSampler(getDevice(), &info, nullptr, &_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create texture sampler.");
		}
	}

	const Device& getDevice() const {
		assert(_device);
		return *_device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroySampler(getDevice(), _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~Sampler() { destroy(); }

  private:
	const Device* _device = nullptr;
};
