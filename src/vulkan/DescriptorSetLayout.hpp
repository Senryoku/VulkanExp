#pragma once

#include <stdexcept>

#include "Device.hpp"
#include "HandleWrapper.hpp"

class DescriptorSetLayout : public HandleWrapper<VkDescriptorSetLayout> {
  public:
	DescriptorSetLayout() = default;
	DescriptorSetLayout(const Device& device, const VkDescriptorSetLayoutCreateInfo& info) { create(device, info); }
	DescriptorSetLayout(const DescriptorSetLayout&) = delete;
	DescriptorSetLayout(DescriptorSetLayout&&) noexcept;
	~DescriptorSetLayout() { destroy(); }

	void create(VkDevice device, const VkDescriptorSetLayoutCreateInfo& info);
	void destroy();

  private:
	VkDevice _device = VK_NULL_HANDLE;
};

class DescriptorSetLayoutBuilder {
  public:
	void add(const VkDescriptorSetLayoutBinding& binding) { bindings.push_back(binding); }

	DescriptorSetLayout build(const Device& device) const {
		return DescriptorSetLayout{
			device,
			VkDescriptorSetLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(bindings.size()),
				.pBindings = bindings.data(),
			},
		};
	}

  private:
	std::vector<VkDescriptorSetLayoutBinding> bindings;
};
