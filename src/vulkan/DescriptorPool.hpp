#pragma once

#include <stdexcept>

#include "Buffer.hpp"
#include "DescriptorSetLayout.hpp"
#include "HandleWrapper.hpp"

class DescriptorPool : public HandleWrapper<VkDescriptorPool> {
  public:
	DescriptorPool() = default;
	DescriptorPool(const DescriptorPool&) = delete;
	DescriptorPool(DescriptorPool&& p) noexcept;
	DescriptorPool& operator=(DescriptorPool&& p) noexcept;
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
			.type = type,
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

class DescriptorSetWriter {
  public:
	const size_t					  MaxImages = 1024;
	const size_t					  MaxWritePerType = 32;
	std::vector<VkWriteDescriptorSet> writeDescriptorSets;

	DescriptorSetWriter(VkDescriptorSet descriptorSet) : _descriptorSet(descriptorSet) {
		_imageInfos.reserve(MaxImages);
		_bufferInfos.reserve(MaxWritePerType);
		_bufferViews.reserve(MaxWritePerType);
		_writeAccelerationStructures.reserve(MaxWritePerType);
	}

	DescriptorSetWriter& add(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info) {
		assert(_imageInfos.size() < MaxImages);
		_imageInfos.push_back(info);
		writeDescriptorSets.push_back({
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = _descriptorSet,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = type,
			.pImageInfo = &_imageInfos.back(),
		});
		return *this;
	}

	DescriptorSetWriter& add(uint32_t binding, VkDescriptorType type, const std::vector<VkDescriptorImageInfo>& infos) {
		assert(_imageInfos.size() < MaxImages + 1 - infos.size());
		for(const auto& i : infos)
			_imageInfos.push_back(i);
		if(infos.size() > 0)
			writeDescriptorSets.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = _descriptorSet,
				.dstBinding = binding,
				.descriptorCount = static_cast<uint32_t>(infos.size()),
				.descriptorType = type,
				.pImageInfo = &_imageInfos[_imageInfos.size() - infos.size()],
			});
		return *this;
	}

	DescriptorSetWriter& add(uint32_t binding, VkDescriptorType type, const Buffer& buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE) {
		return add(binding, type,
				   {
					   .buffer = buffer,
					   .offset = 0,
					   .range = VK_WHOLE_SIZE,
				   });
	}

	DescriptorSetWriter& add(uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo& info) {
		assert(_bufferInfos.size() < MaxWritePerType);
		_bufferInfos.push_back(info);
		writeDescriptorSets.push_back({
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = _descriptorSet,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = type,
			.pBufferInfo = &_bufferInfos.back(),
		});
		return *this;
	}

	DescriptorSetWriter& add(uint32_t binding, VkDescriptorType type, const VkBufferView& info) {
		assert(_bufferViews.size() < MaxWritePerType);
		_bufferViews.push_back(info);
		writeDescriptorSets.push_back({
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = _descriptorSet,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = type,
			.pTexelBufferView = &_bufferViews.back(),
		});
		return *this;
	}

	DescriptorSetWriter& add(uint32_t binding, const VkWriteDescriptorSetAccelerationStructureKHR& info) {
		assert(_writeAccelerationStructures.size() < MaxWritePerType);
		_writeAccelerationStructures.push_back(info);
		writeDescriptorSets.push_back({
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = &_writeAccelerationStructures.back(), // The acceleration structure descriptor has to be chained via pNext
			.dstSet = _descriptorSet,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		});
		return *this;
	}

	void update(VkDevice device) { vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE); }

  private:
	VkDescriptorSet											  _descriptorSet = VK_NULL_HANDLE;
	std::vector<VkDescriptorImageInfo>						  _imageInfos;
	std::vector<VkDescriptorBufferInfo>						  _bufferInfos;
	std::vector<VkBufferView>								  _bufferViews;
	std::vector<VkWriteDescriptorSetAccelerationStructureKHR> _writeAccelerationStructures;
};
