#include "Image.hpp"
#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "DeviceMemory.hpp"

Image::Image(const Device& device, const STBImage& image, uint32_t queueFamilyIndex) : _device(&device) {
	upload(image, queueFamilyIndex);
}

Image::~Image() {
	destroy();
}

void Image::destroy() {
	if(isValid()) {
		vkDestroyImage(getDevice(), _handle, nullptr);
		_handle = VK_NULL_HANDLE;
		_memory.free();
	}
}

void Image::create(const Device& device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, bool mipmaps) {
	if(mipmaps)
		_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {width, height, 1},
		.mipLevels = _mipLevels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &_handle));
	_device = &device;
}

VkMemoryRequirements Image::getMemoryRequirements() const {
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(getDevice(), _handle, &memRequirements);
	return memRequirements;
}

void Image::allocate(VkMemoryPropertyFlags properties) {
	auto memReq = getMemoryRequirements();
	_memory.allocate(getDevice(), getDevice().getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, properties), memReq.size);

	if(vkBindImageMemory(getDevice(), _handle, _memory, 0) != VK_SUCCESS) {
		throw std::runtime_error("Error: Call to vkBindImageMemory failed.");
	}
}

void Image::upload(VkCommandBuffer commandBuffer, const Buffer& stagingBuffer, const STBImage& image, VkFormat format) {
	// Copy image data to the staging buffer
	stagingBuffer.getMemory().fill(image.getData(), image.byteSize());

	create(getDevice(), static_cast<uint32_t>(image.getWidth()), static_cast<uint32_t>(image.getHeight()), format, VK_IMAGE_TILING_OPTIMAL,
		   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);
	allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(getDevice().getPhysicalDevice(), format, &formatProperties);

	// Copy to the actual device memory
	transitionLayout(commandBuffer, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region{
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource =
			VkImageSubresourceLayers{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		.imageOffset = VkOffset3D{0, 0, 0},
		.imageExtent =
			VkExtent3D{
				.width = static_cast<unsigned int>(image.getWidth()),
				.height = static_cast<unsigned int>(image.getHeight()),
				.depth = 1,
			},
	};
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, _handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		warn("Texture image format does not support linear blitting.\n");
		transitionLayout(commandBuffer, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
						 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	} else {
		// generateMipmaps takes care of the layout transition.
		generateMipmaps(commandBuffer, static_cast<uint32_t>(image.getWidth()), static_cast<uint32_t>(image.getHeight()));
	}
}

void Image::upload(const STBImage& image, uint32_t queueFamilyIndex, VkFormat format) {
	// Prepare staging buffer
	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(getDevice(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, image.byteSize());
	stagingMemory.allocate(getDevice(), stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// Copy to the actual device memory
	getDevice().submit(queueFamilyIndex, [&](const CommandBuffer& commandBuffer) { upload(commandBuffer, stagingBuffer, image, format); });
}

void Image::transitionLayout(VkCommandBuffer commandBuffer, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcMask,
							 VkPipelineStageFlags dstMask) {
	setLayout(commandBuffer, _handle, oldLayout, newLayout,
			  VkImageSubresourceRange{
				  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				  .baseMipLevel = 0,
				  .levelCount = getMipLevels(),
				  .baseArrayLayer = 0,
				  .layerCount = 1,
			  },
			  srcMask, dstMask);
}

void Image::transitionLayout(uint32_t queueFamilyIndex, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcMask,
							 VkPipelineStageFlags dstMask) {
	getDevice().submit(queueFamilyIndex, [&](const CommandBuffer& commandBuffer) { transitionLayout(commandBuffer, format, oldLayout, newLayout, srcMask, dstMask); });
}

void Image::setLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subSourceRange,
					  VkPipelineStageFlags srcMask, VkPipelineStageFlags dstMask) {
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0, // Set below
		.dstAccessMask = 0, // Set below
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subSourceRange,
	};

	switch(oldLayout) {
		case VK_IMAGE_LAYOUT_UNDEFINED: barrier.srcAccessMask = 0; break;
		case VK_IMAGE_LAYOUT_PREINITIALIZED: barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_GENERAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT; break; // Is it?
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
		default: throw std::invalid_argument("Unsupported layout transition!");
	}

	switch(newLayout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.dstAccessMask = barrier.srcAccessMask == 0 ? VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_GENERAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT; break; // Is it?
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		default: throw std::invalid_argument("Unsupported layout transition!");
	}

	vkCmdPipelineBarrier(commandBuffer, srcMask, dstMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Image::generateMipmaps(uint32_t queueFamilyIndex, int32_t width, int32_t height) {

	getDevice().submit(queueFamilyIndex, [&](const CommandBuffer& commandBuffer) { generateMipmaps(commandBuffer, width, height); });
}

void Image::generateMipmaps(VkCommandBuffer commandBuffer, int32_t width, int32_t height, VkImageLayout initialLayout, VkImageLayout finalLayout,
							VkImageLayout mipmapsInitialLayout) {
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = _handle,
		.subresourceRange =
			VkImageSubresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
	};

	// Set all mipmaps layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL if it's not already the case
	if(mipmapsInitialLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		for(uint32_t i = 1; i < _mipLevels; i++) {
			barrier.subresourceRange.baseMipLevel = i;
			barrier.oldLayout = mipmapsInitialLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

	for(uint32_t i = 1; i < _mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = i == 1 ? initialLayout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkImageBlit blit{
			.srcSubresource =
				VkImageSubresourceLayers{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = i - 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			.srcOffsets = {{0, 0, 0}, {width, height, 1}},
			.dstSubresource =
				VkImageSubresourceLayers{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = i,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			.dstOffsets = {{0, 0, 0}, {width > 1 ? width / 2 : 1, height > 1 ? height / 2 : 1, 1}},
		};

		vkCmdBlitImage(commandBuffer, _handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		// Set this mip level layout as finalLayout as we're done writing to it.
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = finalLayout;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		if(width > 1)
			width /= 2;
		if(height > 1)
			height /= 2;
	}

	barrier.subresourceRange.baseMipLevel = _mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = finalLayout;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
