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
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, texturesCount) // Texture
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)					  // Vertices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)					  // Indices
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1)					  // Instance Offsets
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 1);					  // Materials
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
				.buffer = scene.VertexBuffer,
				.offset = 0,
				.range = scene.NextVertexMemoryOffset,
			});
	// Indices
	dsw.add(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = scene.IndexBuffer,
				.offset = 0,
				.range = scene.NextIndexMemoryOffset,
			});
	// Instance Offsets
	dsw.add(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				.buffer = scene.OffsetTableBuffer,
				.offset = 0,
				.range = scene.OffsetTableSize,
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

struct ShaderBindingTable {
	Buffer		 buffer;
	DeviceMemory memory;

	VkStridedDeviceAddressRegionKHR raygenEntry;
	VkStridedDeviceAddressRegionKHR missEntry;
	VkStridedDeviceAddressRegionKHR anyhitEntry;
	VkStridedDeviceAddressRegionKHR callableEntry;

	void create(const Device& device, const std::array<size_t, 4> entriesCount, VkPipeline pipeline) {
		auto rayTracingPipelineProperties = device.getPhysicalDevice().getRaytracingPipelineProperties();

		const size_t   totalEntries = entriesCount[0] + entriesCount[1] + entriesCount[2] + entriesCount[3];
		const auto	   handle_size = rayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t handle_size_aligned = aligned_size(handle_size, rayTracingPipelineProperties.shaderGroupBaseAlignment);
		const size_t   regionSizes[4] = {
			  entriesCount[0] * handle_size_aligned,
			  entriesCount[1] * handle_size_aligned,
			  entriesCount[2] * handle_size_aligned,
			  entriesCount[3] * handle_size_aligned,
		  };
		auto				 stb_size = regionSizes[0] + regionSizes[1] + regionSizes[2] + regionSizes[3];
		std::vector<uint8_t> shader_handle_storage(stb_size);
		VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, totalEntries, stb_size, shader_handle_storage.data()));

		size_t offsetInShaderHandleStorage = 0;
		if(!buffer) {
			buffer.create(device, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, stb_size);
			memory.allocate(device, buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		size_t offset = 0;
		for(size_t i = 0; i < 4; ++i) {
			if(regionSizes[i] > 0) {
				char* mapped = (char*)memory.map(regionSizes[i], offset);
				for(size_t handleIdx = 0; handleIdx < entriesCount[i]; ++handleIdx) {
					memcpy(mapped + handleIdx * handle_size_aligned, shader_handle_storage.data() + offsetInShaderHandleStorage + handleIdx * handle_size, handle_size);
					offset += handle_size_aligned;
				}
				memory.unmap();
				offsetInShaderHandleStorage += entriesCount[i] * handle_size;
			}
		}

		auto bufferAddr = buffer.getDeviceAddress();
		if(entriesCount[0] > 0)
			raygenEntry = {
				.deviceAddress = bufferAddr,
				.stride = handle_size_aligned,
				.size = regionSizes[0],
			};

		if(entriesCount[1] > 0)
			missEntry = {
				.deviceAddress = bufferAddr + regionSizes[0],
				.stride = handle_size_aligned,
				.size = regionSizes[1],
			};

		if(entriesCount[2] > 0)
			anyhitEntry = {
				.deviceAddress = bufferAddr + regionSizes[0] + regionSizes[1],
				.stride = handle_size_aligned,
				.size = regionSizes[2],
			};

		if(entriesCount[3] > 0)
			callableEntry = {
				.deviceAddress = bufferAddr + regionSizes[0] + regionSizes[1] + regionSizes[2],
				.stride = handle_size_aligned,
				.size = regionSizes[3],
			};
	}

	void destroy() {
		buffer.destroy();
		memory.free();
	}
};
