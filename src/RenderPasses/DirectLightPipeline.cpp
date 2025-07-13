#include "Editor.hpp"

#include <RaytracingDescriptors.hpp>

void Editor::createDirectLightPass() {
	Shader raygenShader(_device, "./shaders_spv/directLight.rgen.spv");
	Shader raymissShader(_device, "./shaders_spv/miss.rmiss.spv");
	Shader raymissShadowShader(_device, "./shaders_spv/shadow.rmiss.spv");
	Shader anyhitShader(_device, "./shaders_spv/anyhit.rahit.spv");

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR),
		raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR),
		raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR),
		anyhitShader.getStageCreateInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
	};

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{
		// Ray generation group
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 0,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		// Ray miss group
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 1,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 2,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		// Ray hit group
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = 3,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
	};

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // 11 Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)	// 12 GBUffer 0
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)	// 13 GBuffer 1
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)	// 14 GBuffer 2
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)	// 15 Result (Direct Light)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR); // 16 Previous Result (Direct Light)

	_directLightDescriptorSetLayout = dslBuilder.build(_device);
	_directLightPipeline.getLayout().create(_device, {_directLightDescriptorSetLayout, _descriptorSetLayouts[0].getHandle()});

	VkRayTracingPipelineCreateInfoKHR raytracingPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.groupCount = static_cast<uint32_t>(shaderGroups.size()),
		.pGroups = shaderGroups.data(),
		.maxPipelineRayRecursionDepth = 1,
		.layout = _directLightPipeline.getLayout(),
	};
	_directLightPipeline.create(_device, raytracingPipelineCreateInfo, _pipelineCache);

	_directLightShaderBindingTable.create(_device, {1, 2, 1, 0}, _directLightPipeline);
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	uint32_t						   setCount = static_cast<uint32_t>(_swapChainImages.size());
	for(size_t i = 0; i < setCount; ++i)
		layoutsToAllocate.push_back(_directLightDescriptorSetLayout);
	_directLightDescriptorPool.create(_device, layoutsToAllocate.size(),
									  std::array<VkDescriptorPoolSize, 5>{
										  VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount},
										  VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5 * setCount},
										  VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024u},
										  VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024u},
										  VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * setCount},
									  });
	_directLightDescriptorPool.allocate(layoutsToAllocate);

	writeDirectLightDescriptorSets();

	DescriptorSetLayoutBuilder filterDSLB;
	filterDSLB.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 3 Previous Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT); // 4 Previous Result
	_directLightFilterDescriptorSetLayout = filterDSLB.build(_device);
	_directLightFilterPipelineX.getLayout().create(_device, {_directLightFilterDescriptorSetLayout});
	Shader						filterShaderX(_device, "./shaders_spv/directLightFilterX.comp.spv");
	VkComputePipelineCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stage = filterShaderX.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
		.layout = _directLightFilterPipelineX.getLayout(),
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = 0,
	};
	_directLightFilterPipelineX.create(_device, info, _pipelineCache);
	_directLightFilterPipelineY.getLayout().create(_device, {_directLightFilterDescriptorSetLayout});
	Shader filterShaderY(_device, "./shaders_spv/directLightFilterY.comp.spv");
	info.stage = filterShaderY.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT);
	info.layout = _directLightFilterPipelineY.getLayout();
	_directLightFilterPipelineY.create(_device, info, _pipelineCache);

	{
		std::vector<VkDescriptorSetLayout> layoutsToAllocate;
		uint32_t						   setCount = 2 * _swapChainImages.size();
		for(size_t i = 0; i < setCount; ++i)
			layoutsToAllocate.push_back(_directLightFilterDescriptorSetLayout);
		_directLightFilterDescriptorPool.create(_device, static_cast<uint32_t>(layoutsToAllocate.size()),
												std::array<VkDescriptorPoolSize, 2>{
													VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * setCount},
													VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * setCount},
												});
		_directLightFilterDescriptorPool.allocate(layoutsToAllocate);
		for(size_t i = 0; i < setCount / 2; ++i) {
			// X
			DescriptorSetWriter writer(_directLightFilterDescriptorPool.getDescriptorSets()[2 * i + 0]);
			writer
				.add(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _gbufferImageViews[_gbufferSize * i + 0],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightIntermediateFilterImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					 {
						 .buffer = _cameraUniformBuffers[_swapChainImages.size() + i],
						 .offset = 0,
						 .range = sizeof(CameraBuffer),
					 })
				.add(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightImageViews[_swapChainImages.size() + i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.update(_device);
			// Y
			DescriptorSetWriter writer2(_directLightFilterDescriptorPool.getDescriptorSets()[2 * i + 1]);
			writer2
				.add(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _gbufferImageViews[_gbufferSize * i + 0],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightIntermediateFilterImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					 {
						 .buffer = _cameraUniformBuffers[_swapChainImages.size() + i],
						 .offset = 0,
						 .range = sizeof(CameraBuffer),
					 })
				.add(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _directLightImageViews[_swapChainImages.size() + i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.update(_device);
		}
	}
}

void Editor::writeDirectLightDescriptorSets() {
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		auto writer = baseSceneWriter(_device, _directLightDescriptorPool.getDescriptorSets()[i], _renderer, _irradianceProbes, _lightUniformBuffers[i]);
		// Camera
		writer.add(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				   {
					   .buffer = _cameraUniformBuffers[i],
					   .offset = 0,
					   .range = sizeof(CameraBuffer),
				   });
		writer.add(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[_gbufferSize * i + 0],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[_gbufferSize * i + 1],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[_gbufferSize * i + 2],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		// Result
		writer.add(15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _directLightImageViews[i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(16, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _directLightImageViews[_swapChainImages.size() + i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });

		writer.update(_device);
	}
}

void Editor::destroyDirectLightPipeline() {
	_directLightPipeline.destroy();
	_directLightDescriptorPool.destroy();
	_directLightDescriptorSetLayout.destroy();

	_directLightFilterPipelineX.destroy();
	_directLightFilterPipelineY.destroy();
	_directLightFilterDescriptorPool.destroy();
	_directLightFilterDescriptorSetLayout.destroy();
}
