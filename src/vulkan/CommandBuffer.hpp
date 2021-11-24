#pragma once

#include <stdexcept>
#include <vector>

#include "CommandPool.hpp"
#include "Framebuffer.hpp"
#include "HandleWrapper.hpp"
#include "RenderPass.hpp"

class CommandBuffer : public HandleWrapper<VkCommandBuffer> {
  public:
	CommandBuffer(VkCommandBuffer handle) : HandleWrapper(handle) {}

	void begin(VkCommandBufferUsageFlags flags = 0) const {
		VkCommandBufferBeginInfo beginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = flags,				// Optional
			.pInheritanceInfo = nullptr // Optional
		};
		if(vkBeginCommandBuffer(_handle, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin recording command buffer!");
		}
	}

	void end() const {
		if(vkEndCommandBuffer(_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record command buffer!");
		}
	}

	void beginRenderPass(const RenderPass& renderPass, const Framebuffer& framebuffer, VkExtent2D extent) const {
		std::array<VkClearValue, 2> clearValues{
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.5f}},
			VkClearValue{.depthStencil = {1.0f, 0}},
		};
		VkRenderPassBeginInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = renderPass,
			.framebuffer = framebuffer,
			.renderArea = {.offset = {0, 0}, .extent = extent},
			.clearValueCount = static_cast<uint32_t>(clearValues.size()),
			.pClearValues = clearValues.data(),
		};

		vkCmdBeginRenderPass(_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	void endRenderPass() const { vkCmdEndRenderPass(_handle); }

	void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) const {
		vkCmdDraw(_handle, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void bind(uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* offsets) const {
		vkCmdBindVertexBuffers(_handle, firstBinding, bindingCount, pBuffers, offsets);
	}

	template<int Count>
	void bind(const std::array<VkBuffer, Count> buffers) const {
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(_handle, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets);
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

		if(vkAllocateCommandBuffers(device, &allocInfo, buffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}

		_buffers.clear();
		for(size_t i = 0; i < count; ++i) {
			_buffers.push_back(buffers[i]);
			_bufferHandles.push_back(buffers[i]);
		}

		_device = device;
		_commandPool = commandPool;
	}

	void free() {
		const auto commandBufferHandles = getBuffersHandles();
		vkFreeCommandBuffers(_device, _commandPool, static_cast<uint32_t>(commandBufferHandles.size()), commandBufferHandles.data());
		_buffers.clear();
		_bufferHandles.clear();
		_device = VK_NULL_HANDLE;
		_commandPool = VK_NULL_HANDLE;
	}

	const CommandBuffer&				operator[](size_t idx) const { return getBuffers()[idx]; }
	const std::vector<CommandBuffer>&	getBuffers() const { return _buffers; }
	const std::vector<VkCommandBuffer>& getBuffersHandles() const { return _bufferHandles; }

	~CommandBuffers() {
		if(_commandPool != VK_NULL_HANDLE && _device != VK_NULL_HANDLE)
			free();
	}

  private:
	VkDevice					 _device = VK_NULL_HANDLE;
	VkCommandPool				 _commandPool = VK_NULL_HANDLE;
	std::vector<CommandBuffer>	 _buffers;
	std::vector<VkCommandBuffer> _bufferHandles;
};
