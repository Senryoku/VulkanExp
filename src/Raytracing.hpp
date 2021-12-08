#pragma once

#include <DescriptorPool.hpp>
#include <glTF.hpp>

inline uint32_t aligned_size(uint32_t value, uint32_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

inline DescriptorSetLayoutBuilder baseDescriptorSetLayout() {
	uint32_t				   texturesCount = Textures.size();
	DescriptorSetLayoutBuilder dslBuilder;
	// Slot for binding top level acceleration structures to the ray generation shader
	dslBuilder.add(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, texturesCount) // Texture
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)						// Vertices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)						// Indices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)						// Instance Offsets
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1);					// Materials
	return dslBuilder;
}

inline DescriptorSetWriter baseSceneWriter(VkDescriptorSet descSet, const glTF& scene, const VkAccelerationStructureKHR& accelerationStructure) {
	DescriptorSetWriter dsw(descSet);

	// Setup the descriptor for binding our top level acceleration structure to the ray tracing shaders
	dsw.add(0, {
				   .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
				   .accelerationStructureCount = 1,
				   .pAccelerationStructures = &accelerationStructure, // &scene.getTLAS(), // FIXME: Should be part of the Scene?
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

	// FIXME: All these buffers should be relative to the scene.
	dsw.add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureInfos);

	// Vertices
	dsw.add(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = Mesh::VertexBuffer,
				.offset = 0,
				.range = Mesh::NextVertexMemoryOffset,
			});
	// Indices
	dsw.add(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = Mesh::IndexBuffer,
				.offset = 0,
				.range = Mesh::NextIndexMemoryOffset,
			});
	// Instance Offsets
	dsw.add(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = Mesh::OffsetTableBuffer,
				.offset = 0,
				.range = Mesh::OffsetTableSize,
			});

	// Materials
	dsw.add(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = MaterialBuffer,
				.offset = 0,
				.range = sizeof(Material::GPUData) * Materials.size(),
			});

	return dsw;
}
