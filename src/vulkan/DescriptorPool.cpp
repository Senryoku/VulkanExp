#include "DescriptorPool.hpp"

DescriptorPool::DescriptorPool(DescriptorPool&& p) noexcept : HandleWrapper(p._handle), _device(p._device), _descriptorSets(std::move(p._descriptorSets)) {
	p._handle = VK_NULL_HANDLE;
	p._device = VK_NULL_HANDLE;
}

DescriptorPool& DescriptorPool::operator=(DescriptorPool&& p) noexcept {
	_handle = p._handle;
	_device = p._device;
	_descriptorSets = std::move(p._descriptorSets);
	p._handle = VK_NULL_HANDLE;
	p._device = VK_NULL_HANDLE;
	return *this;
}
