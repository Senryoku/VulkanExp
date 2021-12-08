#include "IrradianceProbes.hpp"

#include <Raytracing.hpp>

void IrradianceProbes::init(const Device& device, uint32_t familyQueueIndex, glm::vec3 min, glm::vec3 max) {
	_min = min;
	_max = max;
	_color.create(device, ColorResolution * VolumeResolution[0] * VolumeResolution[1], ColorResolution * VolumeResolution[2], VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				  VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
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

	_depth.create(device, DepthResolution * VolumeResolution[0] * VolumeResolution[1], DepthResolution * VolumeResolution[2], VK_FORMAT_R16G16_UNORM, VK_IMAGE_TILING_OPTIMAL,
				  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
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

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR); // Color
	dslBuilder.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR); // Depth
	_descriptorSetLayout = dslBuilder.build(device);

	_descriptorPool.create(device, 1,
						   std::array<VkDescriptorPoolSize, 5>{
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
						   });
	_descriptorPool.allocate({_descriptorSetLayout.getHandle()});

	_device = &device;
}

void IrradianceProbes::writeDescriptorSet(const glTF& scene, VkAccelerationStructureKHR tlas) {
	auto writer = baseSceneWriter(_descriptorPool.getDescriptorSets()[0], scene, tlas);
}

void IrradianceProbes::update(const glTF& scene) {}

void IrradianceProbes::destroy() {
	_descriptorPool.destroy();
	_descriptorSetLayout.destroy();

	_depthView.destroy();
	_depth.destroy();
	_colorView.destroy();
	_color.destroy();
}
