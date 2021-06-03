#pragma once

#include <stdexcept>
#include <vector>

#include "HandleWrapper.hpp"
#include "RenderPass.hpp"
#include "CommandPool.hpp"
#include "Framebuffer.hpp"

class CommandBuffer : public HandleWrapper<VkCommandBuffer> {
public:
	CommandBuffer(VkCommandBuffer handle) : HandleWrapper(handle) {}

	void begin() const {
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = 0, // Optional
			.pInheritanceInfo = nullptr // Optional
		};
		if (vkBeginCommandBuffer(_handle, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin recording command buffer!");
		}
	}

	void end() const {
		if (vkEndCommandBuffer(_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record command buffer!");
		}
	}

	void beginRenderPass(const RenderPass& renderPass, const Framebuffer& framebuffer, VkExtent2D extent) const {
		VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = renderPass,
			.framebuffer = framebuffer,
			.renderArea = {
				.offset = { 0, 0 },
				.extent = extent
			},
			.clearValueCount = 1,
			.pClearValues = &clearColor,
		};

		vkCmdBeginRenderPass(_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	void endRenderPass() const {
		vkCmdEndRenderPass(_handle);
	}

	void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) const {
		vkCmdDraw(_handle, vertexCount, instanceCount, firstVertex, firstInstance);
	}
};

class CommandBuffers {
public:
	void allocate(VkDevice device, const CommandPool& commandPool, size_t count) {
		std::vector<VkCommandBuffer> buffers(count, VK_NULL_HANDLE);

		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = static_cast<uint32_t>(buffers.size()),
		};

		if (vkAllocateCommandBuffers(device, &allocInfo, buffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}

		_buffers.clear();
		for (size_t i = 0; i < count; ++i)
			_buffers.push_back(buffers[i]);

		_device = device;
	}

	const std::vector<CommandBuffer>& getBuffers() const { return _buffers; }

private:
	VkDevice _device;
	std::vector<CommandBuffer> _buffers;
};