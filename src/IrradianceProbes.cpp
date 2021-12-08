#include "IrradianceProbes.hpp"

#include <Raytracing.hpp>
#include <Shader.hpp>

void IrradianceProbes::init(const Device& device, uint32_t familyQueueIndex, glm::vec3 min, glm::vec3 max) {
	_device = &device;

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

	_pipelineLayout.create(device, {_descriptorSetLayout});
	std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

	Shader raygenShader(device, "./shaders_spv/probes_raygen.rgen.spv");
	shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
	Shader raymissShader(device, "./shaders_spv/miss.rmiss.spv");
	shader_stages.push_back(raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader raymissShadowShader(device, "./shaders_spv/shadow.rmiss.spv");
	shader_stages.push_back(raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader closesthitShader(device, "./shaders_spv/closesthit.rchit.spv");
	shader_stages.push_back(closesthitShader.getStageCreateInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));

	// Ray generation group
	shader_groups.push_back({
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.generalShader = 0,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});

	// Ray miss group
	shader_groups.push_back({
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.generalShader = 1,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});
	shader_groups.push_back({
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.generalShader = 2,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});

	// Ray closest hit group
	shader_groups.push_back({
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = 3,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});

	VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shader_stages.size()),
		.pStages = shader_stages.data(),
		.groupCount = static_cast<uint32_t>(shader_groups.size()),
		.pGroups = shader_groups.data(),
		.maxPipelineRayRecursionDepth = 2,
		.layout = _pipelineLayout,
	};
	_pipeline.create(device, pipelineCreateInfo);

	_descriptorPool.create(device, 1,
						   std::array<VkDescriptorPoolSize, 5>{
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
						   });
	_descriptorPool.allocate({_descriptorSetLayout.getHandle()});

	createShaderBindingTable();
	_commandPool.create(device, familyQueueIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	_commandBuffers.allocate(device, _commandPool, 1);
}

void IrradianceProbes::writeDescriptorSet(const glTF& scene, VkAccelerationStructureKHR tlas) {
	auto writer = baseSceneWriter(_descriptorPool.getDescriptorSets()[0], scene, tlas);
	writer.add(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _colorView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _depthView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.update(*_device);
}

void IrradianceProbes::createShaderBindingTable() {
	VkPhysicalDeviceRayTracingPropertiesNV rayTracingPipelineProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2			   deviceProperties{
				   .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
				   .pNext = &rayTracingPipelineProperties,
	   };
	vkGetPhysicalDeviceProperties2(_device->getPhysicalDevice(), &deviceProperties);

	const size_t entriesCount[4] = {
		1, // rgen
		2, // miss
		1, // hit
		0, // callable
	};
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
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(*_device, _pipeline, 0, totalEntries, stb_size, shader_handle_storage.data()));

	size_t offsetInShaderHandleStorage = 0;
	if(!_shaderBindingTable.buffer) {
		_shaderBindingTable.buffer.create(*_device, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
										  stb_size);
		_shaderBindingTable.memory.allocate(*_device, _shaderBindingTable.buffer,
											VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	size_t offset = 0;
	for(size_t i = 0; i < 4; ++i) {
		if(regionSizes[i] > 0) {
			char* mapped = (char*)_shaderBindingTable.memory.map(regionSizes[i], offset);
			for(size_t handleIdx = 0; handleIdx < entriesCount[i]; ++handleIdx) {
				memcpy(mapped + handleIdx * handle_size_aligned, shader_handle_storage.data() + offsetInShaderHandleStorage + handleIdx * handle_size, handle_size);
				offset += handle_size_aligned;
			}
			_shaderBindingTable.memory.unmap();
			offsetInShaderHandleStorage += entriesCount[i] * handle_size;
		}
	}

	auto bufferAddr = _shaderBindingTable.buffer.getDeviceAddress();
	_shaderBindingTable.raygenEntry = {
		.deviceAddress = bufferAddr,
		.stride = handle_size_aligned,
		.size = regionSizes[0],
	};

	_shaderBindingTable.missEntry = {
		.deviceAddress = bufferAddr + regionSizes[0],
		.stride = handle_size_aligned,
		.size = regionSizes[1],
	};

	_shaderBindingTable.anyhitEntry = {
		.deviceAddress = bufferAddr + regionSizes[0] + regionSizes[1],
		.stride = handle_size_aligned,
		.size = regionSizes[2],
	};

	_shaderBindingTable.callableEntry = {};
}

void IrradianceProbes::update(const glTF& scene) {
	VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
		auto& cmdBuff = _commandBuffers.getBuffers()[i];
		cmdBuff.begin();

		vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipeline);
		vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelineLayout, 0, 1, &_descriptorPool.getDescriptorSets()[0], 0, 0);

		vkCmdTraceRaysKHR(cmdBuff, &_shaderBindingTable.raygenEntry, &_shaderBindingTable.missEntry, &_shaderBindingTable.anyhitEntry, &_shaderBindingTable.callableEntry, 32, 32,
						  1);

		VK_CHECK(vkEndCommandBuffer(cmdBuff));
	}
}

void IrradianceProbes::destroy() {
	_shaderBindingTable.destroy();
	_commandBuffers.free();
	_commandPool.destroy();
	_descriptorPool.destroy();
	_descriptorSetLayout.destroy();
	_pipeline.destroy();
	_pipelineLayout.destroy();

	_depthView.destroy();
	_depth.destroy();
	_colorView.destroy();
	_color.destroy();
}
