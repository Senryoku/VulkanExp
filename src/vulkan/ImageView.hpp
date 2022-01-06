#pragma once

#include "HandleWrapper.hpp"

class ImageView : public HandleWrapper<VkImageView> {
  public:
	ImageView() = default;
	ImageView(const ImageView&) = delete;
	ImageView(ImageView&& v) noexcept : HandleWrapper(v._handle), _device(v._device) { v._handle = VK_NULL_HANDLE; }
	ImageView(VkDevice device, VkImage image, VkFormat format) : _device(device) { create(device, image, format); }
	ImageView(VkDevice device, const Image& image, VkFormat format) : _device(device) { create(device, image, format); }

	void create(VkDevice device, VkImage& image, VkFormat format, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT) {
		create(device,
			   {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = format,
				.components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY},
				.subresourceRange = {
					.aspectMask = aspectFlags,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				}});
	}

	void create(VkDevice device, const Image& image, VkFormat format, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT) {
		create(device,
			   {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = format,
				.components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY},
				.subresourceRange = {
					.aspectMask = aspectFlags,
					.baseMipLevel = 0,
					.levelCount = image.getMipLevels(),
					.baseArrayLayer = 0,
					.layerCount = 1,
				}});
	}

	void create(VkDevice device, const VkImageViewCreateInfo& info) {
		VK_CHECK(vkCreateImageView(device, &info, nullptr, &_handle));

		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyImageView(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~ImageView() { destroy(); }

  private:
	VkDevice _device = VK_NULL_HANDLE;
};
