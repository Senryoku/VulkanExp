#include "DescriptorSetLayout.hpp"

#include <array>

void DescriptorSetLayout::create(VkDevice device) {
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

	create(device, layoutInfo);
}

void DescriptorSetLayout::create(VkDevice device, const VkDescriptorSetLayoutCreateInfo& info) {
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &_handle));

	_device = device;
}

void DescriptorSetLayout::destroy() {
	if(isValid()) {
		vkDestroyDescriptorSetLayout(_device, _handle, nullptr);
		_handle = VK_NULL_HANDLE;
	}
}
