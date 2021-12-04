#include "DescriptorSetLayout.hpp"

#include <array>

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& dsl) noexcept : HandleWrapper(dsl._handle), _device(dsl._device) {
	dsl._handle = VK_NULL_HANDLE;
	dsl._device = VK_NULL_HANDLE;
}

DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& dsl) noexcept {
	_handle = dsl._handle;
	_device = dsl._device;
	dsl._handle = VK_NULL_HANDLE;
	dsl._device = VK_NULL_HANDLE;
	return *this;
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
