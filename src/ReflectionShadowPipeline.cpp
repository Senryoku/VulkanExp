#include "Application.hpp"
#include <Raytracing.hpp>

void Application::createReflectionShadowPipeline() {
	Shader raygenShader(_device, "./shaders_spv/reflectionShadow.rgen.spv");
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
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) //  9 Camera
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 10 GBUffer 0
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 11 GBuffer 1
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 12 GBuffer 2
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)										  // 13 Result (Reflections)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR);										  // 14 Result (Direct Light)

	_reflectionShadowDescriptorSetLayout = dslBuilder.build(_device);
	_reflectionShadowPipeline.getLayout().create(_device, {_reflectionShadowDescriptorSetLayout});

	VkRayTracingPipelineCreateInfoKHR raytracingPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.groupCount = static_cast<uint32_t>(shaderGroups.size()),
		.pGroups = shaderGroups.data(),
		.maxPipelineRayRecursionDepth = 2, // We need light occlusion tests for the reflections
		.layout = _reflectionShadowPipeline.getLayout(),
	};
	_reflectionShadowPipeline.create(_device, raytracingPipelineCreateInfo, _pipelineCache);

	_reflectionShadowShaderBindingTable.create(_device, {1, 2, 1, 0}, _reflectionShadowPipeline);
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	uint32_t						   setCount = _swapChainImages.size();
	for(size_t i = 0; i < setCount; ++i)
		layoutsToAllocate.push_back(_reflectionShadowDescriptorSetLayout);
	_reflectionShadowDescriptorPool.create(_device, layoutsToAllocate.size(),
										   std::array<VkDescriptorPoolSize, 5>{
											   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount},
											   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * setCount},
											   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
											   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
											   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * setCount},
										   });
	_reflectionShadowDescriptorPool.allocate(layoutsToAllocate);

	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		auto writer =
			baseSceneWriter(_device, _reflectionShadowDescriptorPool.getDescriptorSets()[i], _scene, _topLevelAccelerationStructure, _irradianceProbes, _lightUniformBuffers[i]);
		// Camera
		writer.add(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				   {
					   .buffer = _cameraUniformBuffers[i],
					   .offset = 0,
					   .range = sizeof(CameraBuffer),
				   });
		writer.add(11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[3 * i + 0],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[3 * i + 1],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _gbufferImageViews[3 * i + 2],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		// Result
		writer.add(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _reflectionImageViews[i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });
		writer.add(15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				   {
					   .imageView = _directLightImageViews[i],
					   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				   });

		writer.update(_device);
	}
}
