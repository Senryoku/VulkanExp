#pragma once

#include <Buffer.hpp>
#include <DeviceMemory.hpp>

inline uint32_t aligned_size(uint32_t value, uint32_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

struct ShaderBindingTable {
	Buffer		 buffer;
	DeviceMemory memory;

	VkStridedDeviceAddressRegionKHR raygenEntry;
	VkStridedDeviceAddressRegionKHR missEntry;
	VkStridedDeviceAddressRegionKHR anyhitEntry;
	VkStridedDeviceAddressRegionKHR callableEntry;

	void create(const Device& device, const std::array<uint32_t, 4> entriesCount, VkPipeline pipeline) {
		auto rayTracingPipelineProperties = device.getPhysicalDevice().getRaytracingPipelineProperties();

		const uint32_t totalEntries = entriesCount[0] + entriesCount[1] + entriesCount[2] + entriesCount[3];
		const auto	   handle_size = rayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t handle_size_aligned = aligned_size(handle_size, rayTracingPipelineProperties.shaderGroupBaseAlignment);
		const uint32_t regionSizes[4] = {
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
