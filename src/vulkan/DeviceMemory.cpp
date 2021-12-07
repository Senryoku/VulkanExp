#include "DeviceMemory.hpp"

#include <cassert>

#include <fmt/format.h>

DeviceMemory::~DeviceMemory() {
	free();
}

void DeviceMemory::allocate(VkDevice device, const VkMemoryAllocateInfo& allocationInfo) {
	assert(_handle == VK_NULL_HANDLE);
	if(vkAllocateMemory(device, &allocationInfo, nullptr, &_handle) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate memory!");
	}
	_device = device;
}

void DeviceMemory::allocate(VkDevice device, uint32_t memoryTypeIndex, size_t size, VkMemoryAllocateFlags flags) {
	assert(_handle == VK_NULL_HANDLE);

	VkMemoryAllocateFlagsInfo flagInfo{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = VK_NULL_HANDLE, .flags = flags};

	VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = flags > 0 ? &flagInfo : VK_NULL_HANDLE,
		.allocationSize = size,
		.memoryTypeIndex = memoryTypeIndex,
	};

	allocate(device, allocInfo);
}

void DeviceMemory::allocate(const Device& device, const Buffer& buffer, uint32_t memoryTypeIndex, VkMemoryAllocateFlags flags) {
	const auto memReq = buffer.getMemoryRequirements();
	allocate(device, device.getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, memoryTypeIndex), memReq.size, flags);
	vkBindBufferMemory(device, buffer, _handle, 0);
}

void DeviceMemory::free() {
	if(isValid()) {
		vkFreeMemory(_device, _handle, nullptr);
		_handle = VK_NULL_HANDLE;
	}
}

[[nodiscard]] void* DeviceMemory::map(size_t size, size_t offset) const {
	void* data;
	VK_CHECK(vkMapMemory(_device, _handle, offset, size, 0, &data));
	return data;
}

void DeviceMemory::unmap() const {
	vkUnmapMemory(_device, _handle);
}
