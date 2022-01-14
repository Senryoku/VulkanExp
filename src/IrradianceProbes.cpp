#include "IrradianceProbes.hpp"

#include <Raytracing.hpp>
#include <Shader.hpp>

struct PushConstant {
	glm::mat4 orientation;
};

void IrradianceProbes::init(const Device& device, uint32_t transfertFamilyQueueIndex, uint32_t computeFamilyQueueIndex, glm::vec3 min, glm::vec3 max) {
	_device = &device;
	GridParameters.extentMin = min;
	GridParameters.extentMax = max;

	_probesToUpdate.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, getProbeCount() * sizeof(uint32_t));
	_probesToUpdateMemory.allocate(device, _probesToUpdate, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

	// Large enough to update all probes time at MaxRaysPerProbe rays per probe at once.
	// FIXME: Could be resized dynamically depending on GridParameters (layersPerUpdate and raysPerProbe)
	_rayIrradianceDepth.create(device, getProbeCount(), MaxRaysPerProbe, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
							   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT /* For Debug View */);
	_rayIrradianceDepth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_rayIrradianceDepthView.create(device, _rayIrradianceDepth, VK_FORMAT_R32G32B32A32_SFLOAT);
	_rayDirection.create(device, 1, MaxRaysPerProbe, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
						 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT /* For Debug View */);
	_rayDirection.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_rayDirectionView.create(device, _rayDirection, VK_FORMAT_R32G32B32A32_SFLOAT);
	///////////////////////////

	auto wholeImage = VkImageSubresourceRange{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	_workIrradiance.create(device, GridParameters.colorRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.colorRes * GridParameters.resolution[2],
						   VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_workIrradiance.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_workIrradianceView.create(device, VkImageViewCreateInfo{
										   .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
										   .image = _workIrradiance,
										   .viewType = VK_IMAGE_VIEW_TYPE_2D,
										   .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
										   .subresourceRange = wholeImage,
									   });

	_irradiance.create(device, GridParameters.colorRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.colorRes * GridParameters.resolution[2],
					   VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	_irradiance.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_irradianceView.create(device, VkImageViewCreateInfo{
									   .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
									   .image = _irradiance,
									   .viewType = VK_IMAGE_VIEW_TYPE_2D,
									   .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
									   .subresourceRange = wholeImage,
								   });

	_workDepth.create(device, GridParameters.depthRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.depthRes * GridParameters.resolution[2],
					  VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_workDepth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_workDepthView.create(device, VkImageViewCreateInfo{
									  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
									  .image = _workDepth,
									  .viewType = VK_IMAGE_VIEW_TYPE_2D,
									  .format = VK_FORMAT_R16G16_SFLOAT,
									  .subresourceRange = wholeImage,
								  });

	_depth.create(device, GridParameters.depthRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.depthRes * GridParameters.resolution[2],
				  VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	_depth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthView.create(device, VkImageViewCreateInfo{
								  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								  .image = _depth,
								  .viewType = VK_IMAGE_VIEW_TYPE_2D,
								  .format = VK_FORMAT_R16G16_SFLOAT,
								  .subresourceRange = wholeImage,
							  });

	// Transition image to their expected layout
	device.submit(transfertFamilyQueueIndex, [&](const CommandBuffer& b) {
		_rayIrradianceDepth.transitionLayout(b, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		_rayDirection.transitionLayout(b, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		_workIrradiance.transitionLayout(b, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		_irradiance.transitionLayout(b, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		_workDepth.transitionLayout(b, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		_depth.transitionLayout(b, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	});

	_gridInfoBuffer.create(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GridInfo));
	_gridInfoMemory.allocate(device, _gridInfoBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	_probeInfoBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, getProbeCount() * sizeof(ProbeInfo));
	_probeInfoMemory.allocate(device, _probeInfoBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	updateUniforms();

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)   // Ray Irradiance Depth
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)   // Ray Direction
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)									   // Color
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)									   // Depth
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT); // Probes Indices
	_descriptorSetLayout = dslBuilder.build(device);

	_pipelineLayout.create(device, {_descriptorSetLayout},
						   {{
							   // Push Constants
							   .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT,
							   .offset = 0,
							   .size = sizeof(PushConstant),
						   }});

	_descriptorPool.create(device, 1,
						   std::array<VkDescriptorPoolSize, 5>{
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
							   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
						   });
	_descriptorPool.allocate({_descriptorSetLayout.getHandle()});
	_fence.create(device);

	_commandPool.create(device, computeFamilyQueueIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	_commandBuffers.allocate(device, _commandPool, 1);
}

void IrradianceProbes::updateUniforms() {
	_gridInfoMemory.fill(&GridParameters, 1);
}

void IrradianceProbes::destroyPipeline() {
	_traceRaysPipeline.destroy();
	_pipelineProbeInit.destroy();
	_updateIrradiancePipeline.destroy();
	_updateDepthPipeline.destroy();
	_copyBordersPipeline.destroy();
	_queryPool.destroy();
}

void IrradianceProbes::createPipeline() {
	if(_traceRaysPipeline)
		destroyPipeline();

	// Init Pipeline
	{
		std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
		std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

		Shader raygenShader(*_device, "./shaders_spv/probesInit.rgen.spv");
		shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
		Shader closesthitShader(*_device, "./shaders_spv/backfaceTest.rchit.spv");
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

		// Ray closest hit group
		shader_groups.push_back({
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = 1,
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
		_pipelineProbeInit.create(*_device, pipelineCreateInfo);
	}

	// Update Pipeline
	{
		std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
		std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

		Shader raygenShader(*_device, "./shaders_spv/traceProbes.rgen.spv");
		shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
		Shader raymissShader(*_device, "./shaders_spv/miss.rmiss.spv");
		shader_stages.push_back(raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
		Shader raymissShadowShader(*_device, "./shaders_spv/shadow.rmiss.spv");
		shader_stages.push_back(raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
		Shader closesthitShader(*_device, "./shaders_spv/closesthit_noreflection.rchit.spv");
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
		_traceRaysPipeline.create(*_device, pipelineCreateInfo);
	}

	Shader updateIrradianceShader(*_device, "./shaders_spv/probesUpdateIrradiance.comp.spv");
	_updateIrradiancePipeline.create(*_device, VkComputePipelineCreateInfo{
												   .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
												   .pNext = nullptr,
												   .flags = 0,
												   .stage = updateIrradianceShader.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
												   .layout = _pipelineLayout,
												   .basePipelineHandle = VK_NULL_HANDLE,
												   .basePipelineIndex = 0,
											   });
	Shader updateDepthShader(*_device, "./shaders_spv/probesUpdateDepth.comp.spv");
	_updateDepthPipeline.create(*_device, VkComputePipelineCreateInfo{
											  .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
											  .pNext = nullptr,
											  .flags = 0,
											  .stage = updateDepthShader.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
											  .layout = _pipelineLayout,
											  .basePipelineHandle = VK_NULL_HANDLE,
											  .basePipelineIndex = 0,
										  });
	Shader copyBordersShader(*_device, "./shaders_spv/probesCopyBorders.comp.spv");
	_copyBordersPipeline.create(*_device, VkComputePipelineCreateInfo{
											  .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
											  .pNext = nullptr,
											  .flags = 0,
											  .stage = copyBordersShader.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
											  .layout = _pipelineLayout,
											  .basePipelineHandle = VK_NULL_HANDLE,
											  .basePipelineIndex = 0,
										  });

	createShaderBindingTable();

	_queryPool.create(*_device, VK_QUERY_TYPE_TIMESTAMP, 5);
}

void IrradianceProbes::writeDescriptorSet(const Scene& scene, VkAccelerationStructureKHR tlas, const Buffer& lightBuffer) {
	auto writer = baseSceneWriter(*_device, _descriptorPool.getDescriptorSets()[0], scene, tlas, *this, lightBuffer);
	writer.add(11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _rayIrradianceDepthView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _rayDirectionView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _workIrradianceView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _workDepthView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {.buffer = _probesToUpdate, .offset = 0, .range = VK_WHOLE_SIZE});
	writer.update(*_device);
}

void IrradianceProbes::setLightBuffer(const Buffer& lightBuffer) {
	_lightBuffer = &lightBuffer;
}

void IrradianceProbes::writeLightDescriptor() {
	DescriptorSetWriter writer(_descriptorPool.getDescriptorSets()[0]);
	writer.add(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			   {
				   .buffer = *_lightBuffer,
				   .offset = 0,
				   .range = sizeof(LightBuffer),
			   });
	writer.update(*_device);
}

void IrradianceProbes::createShaderBindingTable() {
	_shaderBindingTable.create(*_device, {1, 2, 1, 0}, _traceRaysPipeline);
	_probeInitShaderBindingTable.create(*_device, {1, 0, 1, 0}, _pipelineProbeInit);
}

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>

void genBasis(const glm::vec3& n, glm::vec3& b1, glm::vec3& b2) {
	if(n.x > 0.9f)
		b1 = glm::vec3(0.0f, 1.0f, 0.0f);
	else
		b1 = glm::vec3(1.0f, 0.0f, 0.0f);
	b1 -= n * glm::dot(b1, n);
	b1 = glm::normalize(b1);
	b2 = glm::cross(n, b1);
}

void IrradianceProbes::initProbes(VkQueue queue) {
	GridParameters.hysteresis = 0.0f;
	VK_CHECK(vkWaitForFences(*_device, 1, &_fence.getHandle(), VK_TRUE, UINT64_MAX));
	// Get a random orientation to start the sampling spiral from. Generate a orthonormal basis from a random unit vector.
	glm::vec3 Z = glm::sphericalRand(1.0f); // (not randomly seeded)
	glm::vec3 X, Y;
	genBasis(Z, X, Y);
	static uint32_t yOffset = 0;
	PushConstant	pc{
		   .orientation = glm::transpose(glm::mat3(X, Y, Z)),
	   };

	auto& cmdBuff = _commandBuffers.getBuffers()[0];
	cmdBuff.begin();
	vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelineProbeInit);
	vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelineLayout, 0, 1, &_descriptorPool.getDescriptorSets()[0], 0, 0);
	vkCmdPushConstants(cmdBuff, _pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant), &pc);
	vkCmdTraceRaysKHR(cmdBuff, &_probeInitShaderBindingTable.raygenEntry, &_probeInitShaderBindingTable.missEntry, &_probeInitShaderBindingTable.anyhitEntry,
					  &_probeInitShaderBindingTable.callableEntry, GridParameters.resolution.x, GridParameters.resolution.y, GridParameters.resolution.z);
	cmdBuff.end();

	VkPipelineStageFlags stages[] = {VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR};

	// We'll probably need some sort of synchronization.
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &_commandBuffers.getBuffersHandles()[0],
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr,
	};

	VK_CHECK(vkResetFences(*_device, 1, &_fence.getHandle()));
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, _fence));
}

uint32_t IrradianceProbes::selectProbesToUpdate() {
	static uint32_t s_LoopIndex = 0; // Used to trigger updates of slowly updating probes.

	// Get probe states back.
	auto buffSize = getProbeCount() * sizeof(ProbeInfo);
	auto data = _probeInfoMemory.map(buffSize);
	_probesState.resize(getProbeCount());
	memcpy(_probesState.data(), data, buffSize);
	_probeInfoMemory.unmap();

	std::vector<uint32_t> toUpdate;
	toUpdate.reserve(_probesState.size());
	uint32_t idx = _lastUpdateOffset;
	uint32_t checkedProbes = 0;
	while(checkedProbes < _probesState.size() && (ProbesPerUpdate == 0 || toUpdate.size() < ProbesPerUpdate)) {
		if(_probesState[idx].state != 0 && ((idx + s_LoopIndex) % _probesState[idx].state) == 0)
			toUpdate.push_back(idx);
		++idx;
		if(idx >= _probesState.size()) {
			idx = 0;
			++s_LoopIndex;
		}
		++checkedProbes;
	}
	_lastUpdateOffset = idx;
	if(toUpdate.size() > 0)
		_probesToUpdateMemory.fill(toUpdate.data(), toUpdate.size());
	return static_cast<uint32_t>(toUpdate.size());
}

void IrradianceProbes::update(const Scene& scene, VkQueue queue) {
#if 0
	// Decouple the updates from the framerate?
	// FIXME: This doesnt work, and always returns VK_READY, there is probably too much synchronisation somewhere else in the program.
	auto result = vkGetFenceStatus(*_device, _fence);
	if(result == VK_NOT_READY) { // Previous update isn't done, try again later.
		return;
	}
	VK_CHECK(result); // Any other result than VK_SUCCESS or VK_NOT_READY is an error.
#else
	VK_CHECK(vkWaitForFences(*_device, 1, &_fence.getHandle(), VK_TRUE, UINT64_MAX));
#endif
	writeLightDescriptor();
	auto probeCount = selectProbesToUpdate();

	if(_queryPool.newSampleFlag) {
		auto queryResults = _queryPool.get();
		if(queryResults.size() >= 5 && queryResults[0].available && queryResults[1].available && queryResults[2].available && queryResults[3].available &&
		   queryResults[4].available) {
			_computeTimes.add(0.000001f * (queryResults[4].result - queryResults[0].result));
			_traceTimes.add(0.000001f * (queryResults[1].result - queryResults[0].result));
			_updateTimes.add(0.000001f * (queryResults[2].result - queryResults[1].result));
			_borderCopyTimes.add(0.000001f * (queryResults[3].result - queryResults[2].result));
			_copyTimes.add(0.000001f * (queryResults[4].result - queryResults[3].result));
			_queryPool.newSampleFlag = false;
		}
	}

	// Get a random orientation to start the sampling spiral from. Generate a orthonormal basis from a random unit vector.
	glm::vec3 Z = glm::sphericalRand(1.0f); // (not randomly seeded)
	glm::vec3 X, Y;
	genBasis(Z, X, Y);
	PushConstant pc{
		.orientation = glm::transpose(glm::mat3(X, Y, Z)),
	};

	// Get closer to the target hysteresis.
	static uint32_t updatedProbes = 0;
	// FIXME: This is wrong. Idealy we should make sure that every probe has been updated at least once before updating the hysteresis, but it require much more bookeeping. I think
	// this is enough for now.
	if(updatedProbes >= getProbeCount()) {
		if(std::abs(GridParameters.hysteresis - TargetHysteresis) > 0.05) {
			updateUniforms();
			GridParameters.hysteresis += 0.1f * (TargetHysteresis - GridParameters.hysteresis);
		} else if(GridParameters.hysteresis != TargetHysteresis) {
			GridParameters.hysteresis = TargetHysteresis;
			updateUniforms();
		}
		updatedProbes -= getProbeCount();
	}
	updatedProbes += probeCount;

	auto& cmdBuff = _commandBuffers.getBuffers()[0];
	cmdBuff.begin();
	_queryPool.reset(cmdBuff);
	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

	vkCmdPushConstants(cmdBuff, _pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant), &pc);

	// Trace Rays
	vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _traceRaysPipeline);
	vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelineLayout, 0, 1, &_descriptorPool.getDescriptorSets()[0], 0, 0);
	vkCmdTraceRaysKHR(cmdBuff, &_shaderBindingTable.raygenEntry, &_shaderBindingTable.missEntry, &_shaderBindingTable.anyhitEntry, &_shaderBindingTable.callableEntry, probeCount,
					  GridParameters.raysPerProbe, 1);

	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = _rayIrradianceDepth,
		.subresourceRange =
			VkImageSubresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
	};
	vkCmdPipelineBarrier(cmdBuff, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 1);

	vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_descriptorPool.getDescriptorSets()[0], 0, 0);
	// Aggregate results into irradiance and depth buffers
	_updateIrradiancePipeline.bind(cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE);
	vkCmdDispatch(cmdBuff, probeCount, 1, 1);
	_updateDepthPipeline.bind(cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE);
	vkCmdDispatch(cmdBuff, probeCount, 1, 1);

	barrier.image = _workIrradiance;
	vkCmdPipelineBarrier(cmdBuff, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	barrier.image = _workDepth;
	vkCmdPipelineBarrier(cmdBuff, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 2);

	// Copy Borders
	_copyBordersPipeline.bind(cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE);
	vkCmdDispatch(cmdBuff, probeCount, 1, 1);

	// Copy the result to the image sampled in the main pipeline (since we update arbitrary probes, the whole texture has to be copied)
	VkImageCopy copy{
		.srcSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		.srcOffset = {0, 0, 0},
		.dstSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		.dstOffset = {0, 0, 0},
		.extent = {GridParameters.colorRes * GridParameters.resolution.x * GridParameters.resolution.y, GridParameters.colorRes * GridParameters.resolution.z, 1},
	};
	VkImageSubresourceRange range{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};
	Image::setLayout(cmdBuff, _workIrradiance, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range);
	Image::setLayout(cmdBuff, _irradiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);

	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 3);

	vkCmdCopyImage(cmdBuff, _workIrradiance, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _irradiance, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	Image::setLayout(cmdBuff, _irradiance, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
	Image::setLayout(cmdBuff, _workIrradiance, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, range);

	copy.extent = {GridParameters.depthRes * GridParameters.resolution.x * GridParameters.resolution.y, GridParameters.depthRes * GridParameters.resolution.z, 1},
	Image::setLayout(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range);
	Image::setLayout(cmdBuff, _depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
	vkCmdCopyImage(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	Image::setLayout(cmdBuff, _depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
	Image::setLayout(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, range);
	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 4);
	cmdBuff.end();

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &_commandBuffers.getBuffersHandles()[0],
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr,
	};

	VK_CHECK(vkResetFences(*_device, 1, &_fence.getHandle()));
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, _fence));
	_queryPool.newSampleFlag = true;
}

void IrradianceProbes::destroy() {
	_shaderBindingTable.destroy();
	_probeInitShaderBindingTable.destroy();
	_fence.destroy();
	_commandBuffers.free();
	_commandPool.destroy();
	_gridInfoMemory.free();
	_gridInfoBuffer.destroy();
	_probeInfoBuffer.destroy();
	_probeInfoMemory.free();
	_probesToUpdate.destroy();
	_probesToUpdateMemory.free();
	_descriptorPool.destroy();
	_descriptorSetLayout.destroy();
	destroyPipeline();
	_pipelineLayout.destroy();

	_rayIrradianceDepthView.destroy();
	_rayIrradianceDepth.destroy();
	_rayDirectionView.destroy();
	_rayDirection.destroy();

	_workDepthView.destroy();
	_workDepth.destroy();
	_workIrradianceView.destroy();
	_workIrradiance.destroy();

	_depthView.destroy();
	_depth.destroy();
	_irradianceView.destroy();
	_irradiance.destroy();
}
