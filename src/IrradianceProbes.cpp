#include "IrradianceProbes.hpp"

#include <Raytracing.hpp>
#include <Shader.hpp>

void IrradianceProbes::init(const Device& device, uint32_t transfertFamilyQueueIndex, uint32_t computeFamilyQueueIndex, glm::vec3 min, glm::vec3 max) {
	_device = &device;
	GridParameters.extentMin = min;
	GridParameters.extentMax = max;

	_workColor.create(device, GridParameters.colorRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.colorRes * GridParameters.resolution[2],
					  VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_workColor.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_workColorView.create(device, VkImageViewCreateInfo{
									  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
									  .image = _workColor,
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
	_workColor.transitionLayout(transfertFamilyQueueIndex, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	_color.create(device, GridParameters.colorRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.colorRes * GridParameters.resolution[2],
				  VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
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
	_color.transitionLayout(transfertFamilyQueueIndex, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	_workDepth.create(device, GridParameters.depthRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.depthRes * GridParameters.resolution[2],
					  VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_workDepth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_workDepthView.create(device, VkImageViewCreateInfo{
									  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
									  .image = _workDepth,
									  .viewType = VK_IMAGE_VIEW_TYPE_2D,
									  .format = VK_FORMAT_R16G16_SFLOAT,
									  .subresourceRange =
										  VkImageSubresourceRange{
											  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
											  .baseMipLevel = 0,
											  .levelCount = 1,
											  .baseArrayLayer = 0,
											  .layerCount = 1,
										  },
								  });
	_workDepth.transitionLayout(transfertFamilyQueueIndex, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	_depth.create(device, GridParameters.depthRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.depthRes * GridParameters.resolution[2],
				  VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	_depth.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthView.create(device, VkImageViewCreateInfo{
								  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								  .image = _depth,
								  .viewType = VK_IMAGE_VIEW_TYPE_2D,
								  .format = VK_FORMAT_R16G16_SFLOAT,
								  .subresourceRange =
									  VkImageSubresourceRange{
										  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
										  .baseMipLevel = 0,
										  .levelCount = 1,
										  .baseArrayLayer = 0,
										  .layerCount = 1,
									  },
							  });
	_depth.transitionLayout(transfertFamilyQueueIndex, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	_gridInfoBuffer.create(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GridInfo));
	_gridInfoMemory.allocate(device, _gridInfoBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	updateUniforms();

	DescriptorSetLayoutBuilder dslBuilder = baseDescriptorSetLayout();
	dslBuilder
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)	// Color
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR); // Depth
	_descriptorSetLayout = dslBuilder.build(device);

	_pipelineLayout.create(device, {_descriptorSetLayout},
						   {{
							   // Push Constants
							   .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
							   .offset = 0,
							   .size = sizeof(glm::mat3),
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

void IrradianceProbes::createPipeline() {
	if(_pipeline)
		_pipeline.destroy();
	if(_queryPool)
		_queryPool.destroy();

	std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

	Shader raygenShader(*_device, "./shaders_spv/probes_raygen.rgen.spv");
	shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
	Shader raymissShader(*_device, "./shaders_spv/miss.rmiss.spv");
	shader_stages.push_back(raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader raymissShadowShader(*_device, "./shaders_spv/shadow.rmiss.spv");
	shader_stages.push_back(raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader closesthitShader(*_device, "./shaders_spv/closesthit.rchit.spv");
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
	_pipeline.create(*_device, pipelineCreateInfo);

	createShaderBindingTable();

	_queryPool.create(*_device, VK_QUERY_TYPE_TIMESTAMP, 2);
}

void IrradianceProbes::writeDescriptorSet(const glTF& scene, VkAccelerationStructureKHR tlas, const Buffer& lightBuffer) {
	auto writer = baseSceneWriter(*_device, _descriptorPool.getDescriptorSets()[0], scene, tlas, *this, lightBuffer);
	writer.add(10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _workColorView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.add(11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {.imageView = _workDepthView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL});
	writer.update(*_device);
}

void IrradianceProbes::setLightBuffer(const Buffer& lightBuffer) {
	_lightBuffer = &lightBuffer;
}

void IrradianceProbes::writeLightDescriptor() {
	DescriptorSetWriter writer(_descriptorPool.getDescriptorSets()[0]);
	writer.add(9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			   {
				   .buffer = *_lightBuffer,
				   .offset = 0,
				   .range = sizeof(LightBuffer),
			   });
	writer.update(*_device);
}

void IrradianceProbes::createShaderBindingTable() {
	_shaderBindingTable.create(*_device, {1, 2, 1, 0}, _pipeline);
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

void IrradianceProbes::update(const glTF& scene, VkQueue queue) {
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

	if(_queryPool.newSampleFlag) {
		auto queryResults = _queryPool.get();
		if(queryResults.size() > 1 && queryResults[0].available && queryResults[1].available) {
			_computeTimes.add(0.000001 * (queryResults[1].result - queryResults[0].result));
			_queryPool.newSampleFlag = false;
		}
	}

	// Get a random orientation to start the sampling spiral from. Generate a orthonormal basis from a random unit vector.
	glm::vec3 Z = glm::sphericalRand(1.0f); // (not randomly seeded)
	glm::vec3 X, Y;
	genBasis(Z, X, Y);
	glm::mat3 orientation = glm::transpose(glm::mat3(X, Y, Z));

	auto& cmdBuff = _commandBuffers.getBuffers()[0];
	cmdBuff.begin();
	_queryPool.reset(cmdBuff);
	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
	vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipeline);
	vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelineLayout, 0, 1, &_descriptorPool.getDescriptorSets()[0], 0, 0);
	vkCmdPushConstants(cmdBuff, _pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(glm::mat3), &orientation);
	vkCmdTraceRaysKHR(cmdBuff, &_shaderBindingTable.raygenEntry, &_shaderBindingTable.missEntry, &_shaderBindingTable.anyhitEntry, &_shaderBindingTable.callableEntry,
					  GridParameters.resolution.x, GridParameters.resolution.y, GridParameters.resolution.z);

	// Copy the result to the image sampled in the main pipeline
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
		.extent = {GridParameters.colorRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.colorRes * GridParameters.resolution[2], 1},
	};
	VkImageSubresourceRange range{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};
	Image::setLayout(cmdBuff, _workColor, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range);
	Image::setLayout(cmdBuff, _color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
	vkCmdCopyImage(cmdBuff, _workColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	Image::setLayout(cmdBuff, _color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
	Image::setLayout(cmdBuff, _workColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, range);

	copy.extent = {GridParameters.depthRes * GridParameters.resolution[0] * GridParameters.resolution[1], GridParameters.depthRes * GridParameters.resolution[2], 1},
	Image::setLayout(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, range);
	Image::setLayout(cmdBuff, _depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
	vkCmdCopyImage(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	Image::setLayout(cmdBuff, _depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
	Image::setLayout(cmdBuff, _workDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, range);
	_queryPool.writeTimestamp(cmdBuff, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
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
	_queryPool.newSampleFlag = true;
}

void IrradianceProbes::destroy() {
	_queryPool.destroy();

	_shaderBindingTable.destroy();
	_fence.destroy();
	_commandBuffers.free();
	_commandPool.destroy();
	_gridInfoMemory.free();
	_gridInfoBuffer.destroy();
	_descriptorPool.destroy();
	_descriptorSetLayout.destroy();
	_pipeline.destroy();
	_pipelineLayout.destroy();

	_workDepthView.destroy();
	_workDepth.destroy();
	_workColorView.destroy();
	_workColor.destroy();

	_depthView.destroy();
	_depth.destroy();
	_colorView.destroy();
	_color.destroy();
}
