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

void Image::create(const Device& device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage) {
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

	_device = &device;
}

VkMemoryRequirements Image::getMemoryRequirements() const {
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(getDevice(), _handle, &memRequirements);
	return memRequirements;
}

void Image::create(size_t width, size_t height) {
	create(getDevice(), width, height);
}

void Image::allocate(VkMemoryPropertyFlags properties) {
	auto memReq = getMemoryRequirements();
	_memory.allocate(getDevice(), getDevice().getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, properties), memReq.size);

	if(vkBindImageMemory(getDevice(), _handle, _memory, 0) != VK_SUCCESS) {
		throw std::runtime_error("Error: Call to vkBindImageMemory failed.");
	}
}

void Image::destroy() {
	if(isValid()) {
		vkDestroyImage(getDevice(), _handle, nullptr);
		_handle = VK_NULL_HANDLE;
		if(_memory) {
			_memory.free();
		}
	}
}

void Image::upload(const STBImage& image, uint32_t queueFamilyIndex) {
	// Prepare staging buffer
	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(getDevice(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, image.byteSize());
	auto memReq = stagingBuffer.getMemoryRequirements();
	stagingMemory.allocate(getDevice(),
						   getDevice().getPhysicalDevice().findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
						   memReq.size);
	vkBindBufferMemory(getDevice(), stagingBuffer, stagingMemory, 0);
	// Copy image data to the staging buffer
	stagingMemory.fill(image.getData(), image.byteSize());

	create(image.getWidth(), image.getHeight());
	allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	transitionLayout(queueFamilyIndex, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Copy to the actual device memory
	getDevice().submit(queueFamilyIndex, [&](const CommandBuffer& commandBuffer) {
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
	});
}

void Image::transitionLayout(uint32_t queueFamilyIndex, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
	getDevice().submit(queueFamilyIndex, [&](const CommandBuffer& commandBuffer) {
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0, // Set below
			.dstAccessMask = 0, // Set below
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = _handle,
			.subresourceRange =
				VkImageSubresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		};

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		} else {
			throw std::invalid_argument("unsupported layout transition!");
		}

		vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	});
}
