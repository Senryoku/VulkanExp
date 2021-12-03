#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DescriptorPool : public HandleWrapper<VkDescriptorPool> {
  public:
	DescriptorPool() = default;
	DescriptorPool(const DescriptorPool&) = delete;
	DescriptorPool(DescriptorPool&& p) noexcept : HandleWrapper(p._handle), _device(p._device), _descriptorSets(std::move(p._descriptorSets)) {
		p._handle = VK_NULL_HANDLE;
		p._device = VK_NULL_HANDLE;
	}
	DescriptorPool& operator=(DescriptorPool&& p) noexcept {
		_handle = p._handle;
		_device = p._device;
		_descriptorSets = std::move(p._descriptorSets);
		p._handle = VK_NULL_HANDLE;
		p._device = VK_NULL_HANDLE;
		return *this;
	}
	~DescriptorPool() { destroy(); }

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
			_device = VK_NULL_HANDLE;
			_descriptorSets.clear();
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

	static VkDescriptorPoolCreateInfo getCreateInfo(uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes) {
		return VkDescriptorPoolCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = static_cast<uint32_t>(maxSets),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data(),
		};
	}

  private:
	VkDevice					 _device = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> _descriptorSets;
};

class DescriptorPoolBuilder {
  public:
	DescriptorPoolBuilder& add(VkDescriptorType type, uint32_t count) {
		_poolSizes.push_back(VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = static_cast<uint32_t>(count),
		});
		return *this;
	}

	DescriptorPool build(VkDevice device, uint32_t maxSets) {
		DescriptorPool r;
		r.create(device, DescriptorPool::getCreateInfo(maxSets, _poolSizes));
		return r;
	}

  private:
	std::vector<VkDescriptorPoolSize> _poolSizes;
};
