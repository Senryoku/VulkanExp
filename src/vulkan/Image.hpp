#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class Image : public HandleWrapper<VkImage> {
  public:
    void create(VkDevice device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if(vkCreateImage(device, &imageInfo, nullptr, &_handle) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image");

        _device = device;
    }

    VkMemoryRequirements getMemoryRequirements() const {
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(_device, _handle, &memRequirements);
        return memRequirements;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyImage(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~Image() {
        destroy();
    }

  private:
    VkDevice _device;
};