#pragma once

#include <Buffer.hpp>
#include <Device.hpp>
#include <DeviceMemory.hpp>

/* Allocator with fixed initial capacity and no deallocation */
class StaticDeviceAllocator {
  public:
	void init(const Device& device, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProperties, size_t capacity, VkMemoryAllocateFlags flags = 0) {
		_device = &device;
		_capacity = capacity;
		_buffer.create(device, usage, _capacity);

		_memory.allocate(device, _buffer, device.getPhysicalDevice().findMemoryType(_buffer.getMemoryRequirements().memoryTypeBits, memProperties), flags);
	}

	void bind(const Buffer& buffer, size_t bufferSize = 0) {
		if(bufferSize == 0)
			bufferSize = buffer.getMemoryRequirements().size;
		vkBindBufferMemory(*_device, buffer, _memory, _size);
		_size += bufferSize;
		assert(_size <= _capacity);
	}

	void free() {
		_buffer.destroy();
		_memory.free();
		_size = 0;
		_capacity = 0;
	}

	inline bool isValid() { return _capacity > 0; }
	inline		operator bool() { return isValid(); }

	inline const Buffer&	   buffer() const { return _buffer; }	  // Returns a view (buffer) on the entire allocated memory
	inline const DeviceMemory& memory() const { return _memory; }	  // Memory pool allocated using init()
	inline size_t			   size() const { return _size; }		  // Returns the size of memory leased to external buffers using bind()
	inline size_t			   capacity() const { return _capacity; } // Returns the size of the memory pool allocated at initialisation

  private:
	const Device* _device = nullptr;
	DeviceMemory  _memory;
	Buffer		  _buffer;
	size_t		  _size = 0;
	size_t		  _capacity = 0;
};
