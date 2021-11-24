#include "Device.hpp"

#include "CommandBuffer.hpp"
#include "CommandPool.hpp"

void Device::submit(uint32_t queueFamilyIndex, std::function<void(const CommandBuffer&)> function) const {
	CommandPool stagingCommandPool;
	stagingCommandPool.create(_handle, queueFamilyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	CommandBuffers stagingCommands;
	stagingCommands.allocate(_handle, stagingCommandPool, 1);
	stagingCommands[0].begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	function(stagingCommands[0]);

	stagingCommands[0].end();
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = stagingCommands.getBuffersHandles().data(),
	};
	VkQueue queue;
	vkGetDeviceQueue(_handle, queueFamilyIndex, 0, &queue);
	vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);
}
