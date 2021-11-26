#include "DeviceMemory.hpp"

#include <cassert>

#include <fmt/format.h>

DeviceMemory::~DeviceMemory() {
	free();
}

void DeviceMemory::allocate(VkDevice device, uint32_t memoryTypeIndex, size_t size) {
	assert(_handle == VK_NULL_HANDLE);

	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = size,
		.memoryTypeIndex = memoryTypeIndex,
	};

	if(vkAllocateMemory(device, &allocInfo, nullptr, &_handle) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate vertex buffer memory!");
	}
	_device = device;
}

void DeviceMemory::allocate(const Device& device, const Buffer& buffer, uint32_t memoryTypeIndex) {
	const auto memReq = buffer.getMemoryRequirements();
	allocate(device, device.getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, memoryTypeIndex), memReq.size);
	vkBindBufferMemory(device, buffer, _handle, 0);
}

void DeviceMemory::free() {
	if(isValid()) {
		vkFreeMemory(_device, _handle, nullptr);
		_handle = VK_NULL_HANDLE;
	}
}

[[nodiscard]] void* DeviceMemory::map(size_t size) const {
	void* data;
	vkMapMemory(_device, _handle, 0, size, 0, &data);
	return data;
}

void DeviceMemory::unmap() const {
	vkUnmapMemory(_device, _handle);
}
