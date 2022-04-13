#pragma once

#include <Buffer.hpp>
#include <Device.hpp>
#include <DeviceMemory.hpp>

/* Allocator with fixed initial capacity and no deallocation */
class StaticDeviceAllocator {
  public:
	void init(const Device& device, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProperties, size_t capacity, VkMemoryAllocateFlags flags = 0, size_t expectedFragments = 0) {
		_device = &device;
		_capacity = capacity;
		_bufferUsage = usage;
		_buffer.create(device, usage, _capacity);
		auto memReq = _buffer.getMemoryRequirements();
		// Try to respect alignment constraints given the supplied expected number of fragments
		if(expectedFragments > 0 && (_capacity / expectedFragments) % memReq.alignment != 0) {
			_capacity = expectedFragments * ((_capacity / expectedFragments) + memReq.alignment - ((_capacity / expectedFragments) % memReq.alignment));
			_buffer.destroy();
			_buffer.create(device, usage, _capacity);
			memReq = _buffer.getMemoryRequirements();
		}
		_memory.allocate(device, _buffer, device.getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, memProperties), flags);
	}

	void free() {
		_buffer.destroy();
		_memory.free();
		_size = 0;
		_capacity = 0;
	}

	void bind(Buffer& buffer, size_t bufferSize = 0) {
		if(!buffer && bufferSize > 0)
			buffer.create(*_device, _bufferUsage, bufferSize);
		bind(static_cast<const Buffer&>(buffer), bufferSize);
	}

	void bind(const Buffer& buffer, size_t bufferSize = 0) {
		assert(buffer);
		auto memReq = buffer.getMemoryRequirements();
		if(bufferSize == 0)
			bufferSize = memReq.size;
		vkBindBufferMemory(*_device, buffer, _memory, _size);
		_size += bufferSize;
		if(_size % memReq.alignment != 0)
			_size += (memReq.alignment - _size % memReq.alignment);
		assert(_size <= _capacity);
	}
	// Reserve the next 'size' bytes of memory
	void reserve(size_t size) {
		assert(_size + size <= _capacity);
		_size += size;
	}

	inline bool				   isValid() { return _capacity > 0; }
	inline					   operator bool() { return isValid(); }
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

	VkBufferUsageFlags _bufferUsage;
};
