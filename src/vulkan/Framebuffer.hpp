#pragma once

#include <array>
#include <stdexcept>
#include <vector>

#include "HandleWrapper.hpp"
#include "RenderPass.hpp"

class Framebuffer : public HandleWrapper<VkFramebuffer> {
  public:
    void create(VkDevice device, const RenderPass& renderPass, VkImageView attachment, VkExtent2D extent) {
        create(device, renderPass, 1, &attachment, extent);
    }

    template <uint32_t Count> void create(VkDevice device, const RenderPass& renderPass, const std::array<VkImageView, Count>& attachments, VkExtent2D extent) {
        create(device, renderPass, Count, attachments.data(), extent);
    }

    void create(VkDevice device, const RenderPass& renderPass, const std::vector<VkImageView>& attachments, VkExtent2D extent) {
        create(device, renderPass, static_cast<uint32_t>(attachments.size()), attachments.data(), extent);
    }

    void create(VkDevice device, const RenderPass& renderPass, uint32_t attachmentsCount, const VkImageView* attachments, VkExtent2D extent) {
        VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = attachmentsCount,
            .pAttachments = attachments,
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };

        if(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
        _device = device;
    }

    void destroy() {
        if(isValid()) {
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