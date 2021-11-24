#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DescriptorSetLayout : public HandleWrapper<VkDescriptorSetLayout> {
  public:
	void create(VkDevice device) {
		VkDescriptorSetLayoutBinding uboLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.pImmutableSamplers = nullptr, // Optional
		};

		VkDescriptorSetLayoutBinding samplerLayoutBinding{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr,
		};

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data(),
		};

		if(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create descriptor set layout!");
		}

		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyDescriptorSetLayout(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~DescriptorSetLayout() { destroy(); }

  private:
	VkDevice _device;
};
