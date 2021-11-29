#pragma once

#include <stdexcept>
#include <vector>

#include "Buffer.hpp"
#include "Device.hpp"
#include "HandleWrapper.hpp"

class DeviceMemory : public HandleWrapper<VkDeviceMemory> {
  public:
	DeviceMemory() = default;
	DeviceMemory(const DeviceMemory&) = delete;
	DeviceMemory(DeviceMemory&& m) noexcept : HandleWrapper(m._handle), _device(m._device) { m._handle = VK_NULL_HANDLE; }
	~DeviceMemory();

	void allocate(VkDevice device, const VkMemoryAllocateInfo& allocationInfo);
	void allocate(VkDevice device, uint32_t memoryTypeIndex, size_t size, VkMemoryAllocateFlags flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
	// Allocate memory for the provided buffer and bind them
	void allocate(const Device& device, const Buffer& buffer, uint32_t memoryTypeIndex, VkMemoryAllocateFlags flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
	void free();

	[[nodiscard]] void* map(size_t size) const;
	void				unmap() const;

	template<typename T>
	void fill(const std::vector<T>& data) const {
		fill(data.data(), data.size());
	}

	template<typename T>
	void fill(const T* data, size_t size) const {
		const auto sizeInBytes = sizeof(T) * size;
		auto	   mappedMemory = map(sizeInBytes);
		memcpy(mappedMemory, data, sizeInBytes);
		unmap();
	}

  private:
	VkDevice _device = VK_NULL_HANDLE;
};
