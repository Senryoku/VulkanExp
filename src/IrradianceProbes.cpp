#include "IrradianceProbes.hpp"

void IrradianceProbes::init(const Device& device, uint32_t familyQueueIndex, glm::vec3 min, glm::vec3 max) {
	_min = min;
	_max = max;
	const size_t probesPerLayer = 32 * 32;
	const size_t layersCount = 8;
	_color.create(device, ColorResolution * probesPerLayer, ColorResolution * layersCount, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
	_color.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_colorView.create(device, VkImageViewCreateInfo{
								  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								  .image = _color,
								  .viewType = VK_IMAGE_VIEW_TYPE_2D,
								  .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
								  .subresourceRange =
									  VkImageSubresourceRange{
										  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
										  .baseMipLevel = 0,
										  .levelCount = 1,
										  .baseArrayLayer = 0,
										  .layerCount = 1,
									  },
							  });
	_color.transitionLayout(familyQueueIndex, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	_depth.create(device, DepthResolution * probesPerLayer, DepthResolution * layersCount, VK_FORMAT_R16G16_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
	_depth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthView.create(device, VkImageViewCreateInfo{
								  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								  .image = _depth,
								  .viewType = VK_IMAGE_VIEW_TYPE_2D,
								  .format = VK_FORMAT_R16G16_UNORM,
								  .subresourceRange =
									  VkImageSubresourceRange{
										  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
										  .baseMipLevel = 0,
										  .levelCount = 1,
										  .baseArrayLayer = 0,
										  .layerCount = 1,
									  },
							  });
	_depth.transitionLayout(familyQueueIndex, VK_FORMAT_R16G16_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	_device = &device;
}

void IrradianceProbes::update(const glTF& scene) {}

void IrradianceProbes::destroy() {
	_depthView.destroy();
	_depth.destroy();
	_colorView.destroy();
	_color.destroy();
}
