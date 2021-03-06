#include "Buffer.hpp"

#include <cassert>

#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "DeviceMemory.hpp"

void Buffer::copyFromStagingBuffer(const CommandPool& tmpCommandPool, const Buffer& stagingBuffer, size_t size, VkQueue queue) const {
	CommandBuffers stagingCommands;
	stagingCommands.allocate(_device, tmpCommandPool, 1);
	stagingCommands[0].begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VkBufferCopy copyRegion{
		.srcOffset = 0, // Optional
		.dstOffset = 0, // Optional
		.size = size,
	};
	vkCmdCopyBuffer(stagingCommands[0], stagingBuffer, _handle, 1, &copyRegion);

	stagingCommands[0].end();
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = stagingCommands.getBuffersHandles().data(),
	};
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(queue));
	stagingCommands.free();
}

VkDeviceAddress Buffer::getDeviceAddress() const {
	assert(isValid());
	VkBufferDeviceAddressInfoKHR info{
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = _handle,
	};
	return vkGetBufferDeviceAddressKHR(_device, &info);
}

void Buffer::setMemory(const DeviceMemory& memory, uint32_t offset) {
	_deviceMemory = &memory;
	_offsetInMemory = offset;
}

const DeviceMemory& Buffer::getMemory() const {
	assert(_deviceMemory);
	return *_deviceMemory;
}
