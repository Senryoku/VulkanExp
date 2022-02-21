#include "Editor.hpp"
#include <RaytracingDescriptors.hpp>

void Editor::createReflectionPass() {
	Shader raygenShader(_device, "./shaders_spv/reflection.rgen.spv");
	Shader raymissShader(_device, "./shaders_spv/miss.rmiss.spv");
	Shader raymissShadowShader(_device, "./shaders_spv/shadow.rmiss.spv");
	Shader closesthitShader(_device, "./shaders_spv/closesthit.rchit.spv");
	Shader anyhitShader(_device, "./shaders_spv/anyhit.rahit.spv");

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR),	  raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR),
		raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR), closesthitShader.getStageCreateInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
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
			.closestHitShader = 3,
			.anyHitShader = 4,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
	};

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) // 11 Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 12 GBUffer 0
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 13 GBuffer 1
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 14 GBuffer 2
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR);										  // 15 Result (Reflections)

	_reflectionDescriptorSetLayout = dslBuilder.build(_device);
	_reflectionPipeline.getLayout().create(_device, {_reflectionDescriptorSetLayout, _descriptorSetLayouts[0].getHandle()});

	VkRayTracingPipelineCreateInfoKHR raytracingPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.groupCount = static_cast<uint32_t>(shaderGroups.size()),
		.pGroups = shaderGroups.data(),
		.maxPipelineRayRecursionDepth = 2, // We need light occlusion tests for the reflections
		.layout = _reflectionPipeline.getLayout(),
	};
	_reflectionPipeline.create(_device, raytracingPipelineCreateInfo, _pipelineCache);

	_reflectionShaderBindingTable.create(_device, {1, 2, 1, 0}, _reflectionPipeline);
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	uint32_t						   setCount = _swapChainImages.size();
	for(size_t i = 0; i < setCount; ++i)
		layoutsToAllocate.push_back(_reflectionDescriptorSetLayout);
	_reflectionDescriptorPool.create(_device, layoutsToAllocate.size(),
									 std::array<VkDescriptorPoolSize, 5>{
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * setCount},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * setCount},
									 });
	_reflectionDescriptorPool.allocate(layoutsToAllocate);

	writeReflectionDescriptorSets();

	DescriptorSetLayoutBuilder filterDSLB;
	filterDSLB.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 3 Previous Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT); // 4 Previous Result
	_reflectionFilterDescriptorSetLayout = filterDSLB.build(_device);
	_reflectionFilterPipelineX.getLayout().create(_device, {_reflectionFilterDescriptorSetLayout});
	Shader						filterShaderX(_device, "./shaders_spv/reflectionFilterX.comp.spv");
	VkComputePipelineCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stage = filterShaderX.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
		.layout = _reflectionFilterPipelineX.getLayout(),
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = 0,
	};
	_reflectionFilterPipelineX.create(_device, info);
	_reflectionFilterPipelineY.getLayout().create(_device, {_reflectionFilterDescriptorSetLayout});
	Shader filterShaderY(_device, "./shaders_spv/reflectionFilterY.comp.spv");
	info.stage = filterShaderY.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT);
	info.layout = _reflectionFilterPipelineY.getLayout();
	_reflectionFilterPipelineY.create(_device, info);

	{
		std::vector<VkDescriptorSetLayout> layoutsToAllocate;
		uint32_t						   setCount = 2 * _swapChainImages.size();
		for(size_t i = 0; i < setCount; ++i)
			layoutsToAllocate.push_back(_reflectionFilterDescriptorSetLayout);
		_reflectionFilterDescriptorPool.create(_device, layoutsToAllocate.size(),
											   std::array<VkDescriptorPoolSize, 1>{
												   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * setCount},
											   });
		_reflectionFilterDescriptorPool.allocate(layoutsToAllocate);
		for(size_t i = 0; i < setCount / 2; ++i) {
			// X
			DescriptorSetWriter writer(_reflectionFilterDescriptorPool.getDescriptorSets()[2 * i + 0]);
			writer
				.add(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _gbufferImageViews[3 * i + 0],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _reflectionImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _reflectionIntermediateFilterImageViews[i],
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
						 .imageView = _reflectionImageViews[_swapChainImages.size() + i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.update(_device);
			// Y
			DescriptorSetWriter writer2(_reflectionFilterDescriptorPool.getDescriptorSets()[2 * i + 1]);
			writer2
				.add(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _gbufferImageViews[3 * i + 0],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _reflectionIntermediateFilterImageViews[i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.add(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					 {
						 .imageView = _reflectionImageViews[i],
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
						 .imageView = _reflectionImageViews[_swapChainImages.size() + i],
						 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					 })
				.update(_device);
		}
	}
}

void Editor::writeReflectionDescriptorSets() {
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		auto writer = baseSceneWriter(_device, _reflectionDescriptorPool.getDescriptorSets()[i], _scene, _irradianceProbes, _lightUniformBuffers[i]);
		// Camera
		writer
			.add(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				 {
					 .buffer = _cameraUniformBuffers[i],
					 .offset = 0,
					 .range = sizeof(CameraBuffer),
				 })
			.add(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				 {
					 .imageView = _gbufferImageViews[3 * i + 0],
					 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				 })
			.add(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				 {
					 .imageView = _gbufferImageViews[3 * i + 1],
					 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				 })
			.add(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				 {
					 .imageView = _gbufferImageViews[3 * i + 2],
					 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				 });
		// Result
		writer.add(15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _reflectionImageViews[i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });

		writer.update(_device);
	}
}
