#pragma once

#include <DescriptorPool.hpp>
#include <IrradianceProbes.hpp>
#include <Light.hpp>
#include <Renderer.hpp>

inline DescriptorSetLayoutBuilder baseDescriptorSetLayout() {
	uint32_t				   texturesCount = static_cast<uint32_t>(Textures.size());
	DescriptorSetLayoutBuilder dslBuilder;
	// Slot for binding top level acceleration structures to the ray generation shader
	dslBuilder.add(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, texturesCount)			   // Texture
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)								   // Vertices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)								   // Indices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)								   // Instance Offsets
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1) // Materials
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)		   // Grid Parameters
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)		   // Cell Info
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)															   // Probes Color
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)															   // Probes Depth
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR);	   // Light

	return dslBuilder;
}

// Writes all the necessary descriptors for ray tracing
inline DescriptorSetWriter baseSceneWriter(const Device& device, VkDescriptorSet descSet, const Renderer& renderer, const IrradianceProbes& irradianceProbes,
										   const Buffer& lightBuffer) {
	DescriptorSetWriter dsw(descSet);

	// Setup the descriptor for binding our top level acceleration structure to the ray tracing shaders
	dsw.add(0, {
				   .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
				   .accelerationStructureCount = 1,
				   .pAccelerationStructures = &renderer.getTLAS(),
			   });

	// Bind all textures used in the scene.
	std::vector<VkDescriptorImageInfo> textureInfos;
	for(const auto& texture : Textures) {
		textureInfos.push_back({
			.sampler = *texture.sampler,
			.imageView = texture.gpuImage->imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	}

	dsw.add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureInfos);
	// Vertices
	dsw.add(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, renderer.Vertices.buffer());
	// Indices
	dsw.add(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, renderer.Indices.buffer());
	// Instance Offsets
	dsw.add(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, renderer.OffsetTable.buffer());
	// Materials
	dsw.add(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialBuffer);
	dsw.add(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, irradianceProbes.getGridParametersBuffer(), 0, sizeof(IrradianceProbes::GridInfo));
	dsw.add(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, irradianceProbes.getProbeInfoBuffer());
	dsw.add(
		8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{
			.sampler = *getSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
			.imageView = irradianceProbes.getIrradianceView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	dsw.add(
		9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{
			.sampler = *getSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
			.imageView = irradianceProbes.getDepthView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	dsw.add(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, lightBuffer);

	return dsw;
}
