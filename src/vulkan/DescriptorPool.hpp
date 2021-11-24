#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DescriptorPool : public HandleWrapper<VkDescriptorPool> {
  public:
	DescriptorPool() = default;

	void create(VkDevice device, size_t size) {
		std::array<VkDescriptorPoolSize, 2> poolSizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = static_cast<uint32_t>(size),
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = static_cast<uint32_t>(size),
			},
		};

		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = static_cast<uint32_t>(size),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data(),
		};

		if(vkCreateDescriptorPool(device, &poolInfo, nullptr, &_handle) != VK_SUCCESS)
			throw std::runtime_error("Failed to create descriptor pool.");

		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyDescriptorPool(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	void allocate(size_t count, VkDescriptorSetLayout layout) {
		std::vector<VkDescriptorSetLayout> layouts(count, layout);
		VkDescriptorSetAllocateInfo		   allocInfo{
				   .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				   .descriptorPool = _handle,
				   .descriptorSetCount = static_cast<uint32_t>(count),
				   .pSetLayouts = layouts.data(),
		   };
		_descriptorSets.resize(count);
		if(vkAllocateDescriptorSets(_device, &allocInfo, _descriptorSets.data()) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor sets.");
		}
	}

	const std::vector<VkDescriptorSet>& getDescriptorSets() const { return _descriptorSets; }

	~DescriptorPool() { destroy(); }

  private:
	VkDevice					 _device = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> _descriptorSets;
};
