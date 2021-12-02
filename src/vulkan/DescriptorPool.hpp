#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DescriptorPool : public HandleWrapper<VkDescriptorPool> {
  public:
	DescriptorPool() = default;

	void create(VkDevice device, size_t maxSets) {
		std::array<VkDescriptorPoolSize, 2> poolSizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = static_cast<uint32_t>(maxSets), // FIXME
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = static_cast<uint32_t>(maxSets), // FIXME
			},
		};

		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = static_cast<uint32_t>(2 * maxSets), // FIXME
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data(),
		};

		create(device, poolInfo);
	}

	template<int N>
	void create(VkDevice device, uint32_t maxSets, std::array<VkDescriptorPoolSize, N> poolSizes) {
		VkDescriptorPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = static_cast<uint32_t>(maxSets),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data(),
		};

		create(device, poolInfo);
	}

	void create(VkDevice device, const VkDescriptorPoolCreateInfo& info) {
		VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &_handle));
		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyDescriptorPool(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	void allocate(const std::vector<DescriptorSetLayout>& layouts) {
		std::vector<VkDescriptorSetLayout> layoutHandles;
		for(const auto& l : layouts)
			layoutHandles.push_back(l.getHandle());
		allocate(layoutHandles);
	}

	void allocate(const std::vector<VkDescriptorSetLayout>& layouts) {
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = _handle,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data(),
		};
		_descriptorSets.resize(layouts.size());
		VK_CHECK(vkAllocateDescriptorSets(_device, &allocInfo, _descriptorSets.data()));
	}

	const std::vector<VkDescriptorSet>& getDescriptorSets() const { return _descriptorSets; }

	~DescriptorPool() { destroy(); }

  private:
	VkDevice					 _device = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> _descriptorSets;
};
