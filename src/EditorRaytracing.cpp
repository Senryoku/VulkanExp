#include "Editor.hpp"

#include <RaytracingDescriptors.hpp>

void Editor::createStorageImage() {
	_rayTraceStorageImages.resize(_swapChainImages.size());
	_rayTraceStorageImageViews.resize(_swapChainImages.size());
	const auto imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		_rayTraceStorageImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, imageFormat, VK_IMAGE_TILING_OPTIMAL,
										 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		_rayTraceStorageImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_rayTraceStorageImageViews[i].create(_device, VkImageViewCreateInfo{
														  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
														  .image = _rayTraceStorageImages[i],
														  .viewType = VK_IMAGE_VIEW_TYPE_2D,
														  .format = imageFormat,
														  .subresourceRange =
															  VkImageSubresourceRange{
																  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
																  .baseMipLevel = 0,
																  .levelCount = 1,
																  .baseArrayLayer = 0,
																  .layerCount = 1,
															  },
													  });

		_rayTraceStorageImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), imageFormat, VK_IMAGE_LAYOUT_UNDEFINED,
												   VK_IMAGE_LAYOUT_GENERAL);
	}
}

void Editor::createRayTracingPipeline() {
	auto rayTracingPipelineProperties = _device.getPhysicalDevice().getRaytracingPipelineProperties();
	if(rayTracingPipelineProperties.maxRayRecursionDepth <= 2) {
		throw std::runtime_error("VkPhysicalDeviceRayTracingPipelineProperties.maxRayRecursionDepth should be at least 3 for this pipeline.");
	}

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) // Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR);										  // Result
	_rayTracingDescriptorSetLayout = dslBuilder.build(_device);
	_rayTracingPipelineLayout.create(_device, {_rayTracingDescriptorSetLayout, _descriptorSetLayouts[0]});

	/*
		Setup ray tracing shader groups
		Each shader group points at the corresponding shader in the pipeline
	*/
	Shader raygenShader(_device, "./shaders_spv/raygen.rgen.spv");
	Shader raymissShader(_device, "./shaders_spv/miss.rmiss.spv");
	Shader raymissShadowShader(_device, "./shaders_spv/shadow.rmiss.spv");
	Shader closesthitShader(_device, "./shaders_spv/closesthit.rchit.spv");
	Shader anyhitShader(_device, "./shaders_spv/anyhit.rahit.spv");

	std::vector<VkPipelineShaderStageCreateInfo> shader_stages{
		raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR),	  raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR),
		raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR), closesthitShader.getStageCreateInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
		anyhitShader.getStageCreateInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
	};

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

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

	// Ray hit group
	shader_groups.push_back({
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = 3,
		.anyHitShader = 4,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});

	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shader_stages.size()),
		.pStages = shader_stages.data(),
		.groupCount = static_cast<uint32_t>(shader_groups.size()),
		.pGroups = shader_groups.data(),
		.maxPipelineRayRecursionDepth = 3,
		.layout = _rayTracingPipelineLayout,
	};
	_rayTracingPipeline.create(_device, raytracing_pipeline_create_info, _pipelineCache);

	_raytracingShaderBindingTable.create(_device, {1, 2, 1, 0}, _rayTracingPipeline);
}

void Editor::destroyRayTracingPipeline() {
	_rayTracingPipeline.destroy();
	_rayTracingDescriptorPool.destroy();
	_rayTracingDescriptorSetLayout.destroy();
	_rayTracingPipelineLayout.destroy();
}

void Editor::createRaytracingDescriptorSets() {
	assert(_renderer.getTLAS() != VK_NULL_HANDLE);
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		layoutsToAllocate.push_back(_rayTracingDescriptorSetLayout);
	const auto setCount = static_cast<uint32_t>(_swapChainImages.size());
	_rayTracingDescriptorPool.create(_device, static_cast<uint32_t>(layoutsToAllocate.size()),
									 std::array<VkDescriptorPoolSize, 5>{
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 * setCount},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 * setCount},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * setCount},
									 });
	_rayTracingDescriptorPool.allocate(layoutsToAllocate);
	writeRaytracingDescriptorSets();
}

void Editor::writeRaytracingDescriptorSets() {
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		auto writer = baseSceneWriter(_device, _rayTracingDescriptorPool.getDescriptorSets()[i], _renderer, _irradianceProbes, _lightUniformBuffers[i]);
		// Camera
		writer.add(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				   {
					   .buffer = _cameraUniformBuffers[i],
					   .offset = 0,
					   .range = sizeof(CameraBuffer),
				   });
		// Result
		writer.add(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _rayTraceStorageImageViews[i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });

		writer.update(_device);
	}
}

void Editor::recordRayTracingCommands() {
	VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for(size_t i = 0; i < _rayTraceCommandBuffers.getBuffers().size(); i++) {
		auto& cmdBuff = _rayTraceCommandBuffers.getBuffers()[i];
		cmdBuff.begin();
		_mainTimingQueryPools[i].reset(cmdBuff);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 1);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 2);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 3);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 4);
		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 5);

		// Dispatch the ray tracing commands
		vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipeline);
		const auto descSets = {_rayTracingDescriptorPool.getDescriptorSets()[i], _descriptorPool.getDescriptorSets()[i]};
		vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipelineLayout, 0, 2, descSets.begin(), 0, 0);

		vkCmdTraceRaysKHR(cmdBuff, &_raytracingShaderBindingTable.raygenEntry, &_raytracingShaderBindingTable.missEntry, &_raytracingShaderBindingTable.anyhitEntry,
						  &_raytracingShaderBindingTable.callableEntry, _width, _height, 1);

		// Copy ray tracing output to swap chain image

		// Prepare current swap chain image as transfer destination
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);
		// Prepare ray tracing output image as transfer source
		Image::setLayout(cmdBuff, _rayTraceStorageImages[i], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource_range);

		// vkCmdCopyImage doesn't seem to handle the transition to a sRGB target properly, also sRGB formats are not valid for a storage image usage - at least on my plateform.
#if 0
		VkImageCopy copy_region{
			.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.srcOffset = {0, 0, 0},
			.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {_width, _height, 1},
		};
		vkCmdCopyImage(cmdBuff, _rayTraceStorageImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
#else
		// vkCmdBlitImage does handle the transistion to sRGB (and also mismatching bit depth, for example if we want a 32bit buffer while our swapchain image is 16bits).
		VkImageBlit blit{
			.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.srcOffsets = {{0, 0, 0}, {static_cast<int32_t>(_width), static_cast<int32_t>(_height), 1}},
			.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffsets = {{0, 0, 0}, {static_cast<int32_t>(_width), static_cast<int32_t>(_height), 1}},
		};
		vkCmdBlitImage(cmdBuff, _rayTraceStorageImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
					   VK_FILTER_LINEAR);
#endif
		// Transition swap chain image back to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to prepare for UI rendering
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, subresource_range);
		// Transition ray tracing output image back to general layout
		Image::setLayout(cmdBuff, _rayTraceStorageImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresource_range);

		_mainTimingQueryPools[i].writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 6);
		VK_CHECK(vkEndCommandBuffer(cmdBuff));
	}
}
