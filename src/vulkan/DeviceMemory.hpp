#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DeviceMemory : public HandleWrapper<VkDeviceMemory> {
  public:
	DeviceMemory() = default;
	DeviceMemory(const DeviceMemory&) = delete;
	DeviceMemory(DeviceMemory&& m) noexcept : HandleWrapper(m._handle), _device(m._device) { m._handle = VK_NULL_HANDLE; }

	void allocate(VkDevice device, uint32_t memoryTypeIndex, size_t size) {
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

	void free() {
		if(isValid()) {
			vkFreeMemory(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~DeviceMemory() { free(); }

	[[nodiscard]] void* map(size_t size) const {
		void* data;
		vkMapMemory(_device, _handle, 0, size, 0, &data);
		return data;
	}

	void unmap() const { vkUnmapMemory(_device, _handle); }

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
