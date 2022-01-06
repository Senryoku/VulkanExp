#pragma once

#include <stdexcept>

#include "CommandPool.hpp"
#include "HandleWrapper.hpp"
#include <Logger.hpp>

class DeviceMemory;

class Buffer : public HandleWrapper<VkBuffer> {
  public:
	Buffer() = default;
	Buffer(const Buffer&) = delete;
	Buffer(Buffer&& o) noexcept : HandleWrapper(o._handle), _device(o._device) {
		o._handle = VK_NULL_HANDLE;
		o._device = VK_NULL_HANDLE;
	}

	void create(VkDevice device, VkBufferUsageFlags usage, size_t size) {
		VkBufferCreateInfo bufferInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &_handle));

		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyBuffer(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~Buffer() { destroy(); }

	VkMemoryRequirements getMemoryRequirements() const {
		if(!isValid()) {
			warn("Buffer::Call to 'getMemoryRequirements' on an invalid Buffer.");
			return VkMemoryRequirements{};
		}
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(_device, _handle, &memRequirements);
		return memRequirements;
	}

	VkDeviceAddress getDeviceAddress() const;
	void			copyFromStagingBuffer(const CommandPool& tmpCommandPool, const Buffer& stagingBuffer, size_t size, VkQueue queue) const;

	void				setMemory(const DeviceMemory& memory, uint32_t offset = 0);
	const DeviceMemory& getMemory() const;

  private:
	VkDevice			_device = VK_NULL_HANDLE;
	const DeviceMemory* _deviceMemory = nullptr;
	uint32_t			_offsetInMemory = 0;
};
