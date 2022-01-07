#pragma once

#include <cassert>
#include <stdexcept>

#include "Device.hpp"
#include "DeviceMemory.hpp"
#include "HandleWrapper.hpp"
#include <STBImage.hpp>

class Image : public HandleWrapper<VkImage> {
  public:
	Image() = default;
	Image(const Image&) = delete;
	Image(Image&& i) noexcept : HandleWrapper(i._handle), _device(i._device), _memory(std::move(i._memory)), _mipLevels(i._mipLevels) { i._handle = VK_NULL_HANDLE; }
	Image(const Device& device, const STBImage& image, uint32_t queueFamilyIndex);
	~Image();
	void destroy();

	void create(const Device& device, uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
				VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, bool mipmaps = false);
	// Allocate Device Memory dedicated to this image (see member _memory)
	void allocate(VkMemoryPropertyFlags);

	// Records copy commands to commandBuffer using stagingBuffer as an intermediary step
	void upload(VkCommandBuffer commandBuffer, const Buffer& stagingBuffer, const STBImage& image, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
	void upload(const STBImage& image, uint32_t queueIndex, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

	VkMemoryRequirements getMemoryRequirements() const;

	void		  setDevice(const Device& d) { _device = &d; }
	const Device& getDevice() const {
		assert(_device);
		return *_device;
	}

	uint32_t getMipLevels() const { return _mipLevels; }

	void transitionLayout(uint32_t queueFamilyIndex, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
						  VkPipelineStageFlags srcMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkPipelineStageFlags dstMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	void transitionLayout(VkCommandBuffer commandBuffer, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
						  VkPipelineStageFlags srcMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkPipelineStageFlags dstMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	void generateMipmaps(uint32_t queueFamilyIndex, int32_t width, int32_t height);
	void generateMipmaps(VkCommandBuffer commandBuffer, int32_t width, int32_t height, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						 VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkImageLayout mipmapsInitialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	static void setLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subSourceRange,
						  VkPipelineStageFlags srcMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VkPipelineStageFlags dstMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

  private:
	const Device* _device = nullptr;

	DeviceMemory _memory; // May be unused, depending on the way the image is initialized
	uint32_t	 _mipLevels = 1;
};
