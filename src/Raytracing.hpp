#pragma once

#include <DescriptorPool.hpp>
#include <glTF.hpp>

inline uint32_t aligned_size(uint32_t value, uint32_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

void writeSceneToDescriptorSet(const Device& device, VkDescriptorSet descSet, const glTF& scene, const VkAccelerationStructureKHR& accelerationStructure,
							   std::vector<ImageView*> storage, const Buffer& cameraBuffer) {
	std::vector<VkWriteDescriptorSet> writeDescriptorSets;

	// Setup the descriptor for binding our top level acceleration structure to the ray tracing shaders
	VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &accelerationStructure, // &scene.getTLAS(), // FIXME: Should be part of the Scene?
	};

	VkDescriptorBufferInfo buffer_descriptor{
		.buffer = cameraBuffer,
		.offset = 0,
		.range = sizeof(CameraBuffer),
	};

	// Bind all textures used in the scene.
	std::vector<VkDescriptorImageInfo> textureInfos;
	for(const auto& texture : Textures) {
		textureInfos.push_back(VkDescriptorImageInfo{
			.sampler = *texture.sampler,
			.imageView = texture.gpuImage->imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	}

	// FIXME: All these buffers should be relative to the scene.
	VkDescriptorBufferInfo verticesInfos{
		.buffer = Mesh::VertexBuffer,
		.offset = 0,
		.range = Mesh::NextVertexMemoryOffset,
	};

	VkDescriptorBufferInfo indicesInfos{
		.buffer = Mesh::IndexBuffer,
		.offset = 0,
		.range = Mesh::NextIndexMemoryOffset,
	};

	VkDescriptorBufferInfo offsetsInfos{
		.buffer = Mesh::OffsetTableBuffer,
		.offset = 0,
		.range = Mesh::OffsetTableSize,
	};

	VkDescriptorBufferInfo materialsInfos{
		.buffer = MaterialBuffer,
		.offset = 0,
		.range = sizeof(Material::GPUData) * Materials.size(),
	};

	// Acceleration Structure
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &descriptor_acceleration_structure_info, // The acceleration structure descriptor has to be chained via pNext
		.dstSet = descSet,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	});

	// Camera Buffer
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_descriptor,
	});

	// Textures
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 2,
		.descriptorCount = static_cast<uint32_t>(textureInfos.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = textureInfos.data(),
	});

	// Vertices
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 3,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &verticesInfos,
	});

	// Indices
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 4,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &indicesInfos,
	});

	// Instance Offsets
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 5,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &offsetsInfos,
	});

	// Materials
	writeDescriptorSets.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descSet,
		.dstBinding = 6,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &materialsInfos,
	});

	uint32_t						   binding = 7;
	std::vector<VkDescriptorImageInfo> imageStorageInfos;
	imageStorageInfos.reserve(storage.size());
	for(const auto& imageStorage : storage) {
		imageStorageInfos.push_back({
			.imageView = *imageStorage,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		});
		writeDescriptorSets.push_back({
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descSet,
			.dstBinding = binding++,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &imageStorageInfos.back(),
		});
	}
	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}
