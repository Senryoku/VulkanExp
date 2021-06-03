#pragma once

#include <vector>
#include <stdexcept>

#include "HandleWrapper.hpp"
#include "RenderPass.hpp"

class Framebuffer : public HandleWrapper<VkFramebuffer> {
public:
	void create(VkDevice device, const RenderPass& renderPass, const std::vector<VkImageView>& attachments, VkExtent2D extent) {
		VkFramebufferCreateInfo framebufferInfo{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = renderPass,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.width = extent.width,
			.height = extent.height,
			.layers = 1,
		};

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create framebuffer!");
		}
		_device = device;
	}

	void destroy() {
		if (isValid()) {
			vkDestroyFramebuffer(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~Framebuffer() {
		destroy();
	}
private:
	VkDevice _device = VK_NULL_HANDLE;
};