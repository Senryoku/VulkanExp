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

	void upload(const STBImage& image, uint32_t queueIndex);

	VkMemoryRequirements getMemoryRequirements() const;

	void		  setDevice(const Device& d) { _device = &d; }
	const Device& getDevice() const {
		assert(_device);
		return *_device;
	}

	uint32_t getMipLevels() const { return _mipLevels; }

	void transitionLayout(uint32_t queueFamilyIndex, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
	void generateMipmaps(uint32_t queueFamilyIndex, int32_t width, int32_t height);

  private:
	const Device* _device;

	DeviceMemory _memory; // May be unused, depending on the way the image is initialized
	uint32_t	 _mipLevels = 1;
};
