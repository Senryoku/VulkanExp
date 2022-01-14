#include "Application.hpp"

#include "Raytracing.hpp"

void Application::createStorageImage() {
	_rayTraceStorageImages.resize(_swapChainImages.size());
	_rayTraceStorageImageViews.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		_rayTraceStorageImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
										 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		_rayTraceStorageImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_rayTraceStorageImageViews[i].create(_device, VkImageViewCreateInfo{
														  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
														  .image = _rayTraceStorageImages[i],
														  .viewType = VK_IMAGE_VIEW_TYPE_2D,
														  .format = VK_FORMAT_R16G16B16A16_SFLOAT,
														  .subresourceRange =
															  VkImageSubresourceRange{
																  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
																  .baseMipLevel = 0,
																  .levelCount = 1,
																  .baseArrayLayer = 0,
																  .layerCount = 1,
															  },
													  });

		_rayTraceStorageImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
												   VK_IMAGE_LAYOUT_GENERAL);
	}
}

void Application::createAccelerationStructure() {
	VkFormatProperties2 formatProperties{
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	vkGetPhysicalDeviceFormatProperties2(_physicalDevice, VK_FORMAT_R32G32B32_SFLOAT, &formatProperties);
	assert(formatProperties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR);

	VkTransformMatrixKHR rootTransformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

	size_t submeshesCount = 0;
	for(const auto& m : _scene.getMeshes())
		submeshesCount += m.SubMeshes.size();
	submeshesCount *= 2; // FIXME

	std::vector<uint32_t>									 submeshesIndices;
	std::vector<VkTransformMatrixKHR>						 transforms;
	std::vector<VkAccelerationStructureGeometryKHR>			 geometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 rangeInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*>	 pRangeInfos;
	std::vector<size_t>										 scratchBufferSizes;
	size_t													 scratchBufferSize = 0;
	std::vector<uint32_t>									 blasOffsets; // Start of each BLAS in buffer (aligned to 256 bytes)
	size_t													 totalBLASSize = 0;
	std::vector<VkAccelerationStructureBuildSizesInfoKHR>	 buildSizesInfo;
	geometries.reserve(submeshesCount); // Avoid reallocation since buildInfos will refer to this.
	rangeInfos.reserve(submeshesCount); // Avoid reallocation since pRangeInfos will refer to this.
	blasOffsets.reserve(submeshesCount);
	buildSizesInfo.reserve(submeshesCount);

	const auto&												 meshes = _scene.getMeshes();
	const std::function<void(const Scene::Node&, glm::mat4)> visitNode = [&](const Scene::Node& n, glm::mat4 transform) {
		transform = transform * n.transform;
		for(const auto& c : n.children) {
			visitNode(_scene.getNodes()[c], transform);
		}

		// This is a leaf
		if(n.mesh != -1) {
			transform = glm::transpose(transform); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			for(size_t i = 0; i < meshes[n.mesh].SubMeshes.size(); ++i) {
				submeshesIndices.push_back(meshes[n.mesh].SubMeshes[i].indexIntoOffsetTable);
				transforms.push_back(*reinterpret_cast<VkTransformMatrixKHR*>(&transform));
				/*
				 * Right now there's a one-to-one relation between submeshes and geometries.
				 * This is not garanteed to be optimal (Apparently less BLAS is better, i.e. grouping geometries), but we don't have a mechanism to
				 * retrieve data for distinct geometries (vertices/indices/material) in our ray tracing shaders yet.
				 * This should be doable using the gl_GeometryIndexEXT built-in.
				 */
				geometries.push_back({
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
					.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
					.geometry =
						VkAccelerationStructureGeometryDataKHR{
							.triangles =
								VkAccelerationStructureGeometryTrianglesDataKHR{
									.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
									.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
									.vertexData = meshes[n.mesh].SubMeshes[i].getVertexBuffer().getDeviceAddress(),
									.vertexStride = sizeof(Vertex),
									.maxVertex = static_cast<uint32_t>(meshes[n.mesh].SubMeshes[i].getVertices().size()),
									.indexType = VK_INDEX_TYPE_UINT32,
									.indexData = meshes[n.mesh].SubMeshes[i].getIndexBuffer().getDeviceAddress(),
									.transformData = 0,
								},
						},
					.flags = 0,
				});

				VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
					.pNext = VK_NULL_HANDLE,
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
					.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
					.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
					.srcAccelerationStructure = VK_NULL_HANDLE,
					.geometryCount = 1,
					.pGeometries = &geometries.back(),
					.ppGeometries = nullptr,
				};

				const uint32_t primitiveCount = static_cast<uint32_t>(meshes[n.mesh].SubMeshes[i].getIndices().size() / 3);

				VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
				vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
														&accelerationStructureBuildSizesInfo);
				buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);

				uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
				blasOffsets.push_back(alignedSize);
				totalBLASSize += alignedSize;

				scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

				buildInfos.push_back(accelerationBuildGeometryInfo);

				rangeInfos.push_back({
					.primitiveCount = primitiveCount,
					.primitiveOffset = 0,
					.firstVertex = 0,
					.transformOffset = 0,
				});
			}
		}
	};
	{
		QuickTimer qt("BLAS building");
		// Collect all submeshes and query the memory requirements
		visitNode(_scene.getRoot(), glm::mat4(1.0f));

		_staticBLASBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, totalBLASSize);
		_staticBLASMemory.allocate(_device, _staticBLASBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		size_t runningOffset = 0;
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			// Create the acceleration structure
			VkAccelerationStructureKHR			 blas;
			VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
				.pNext = VK_NULL_HANDLE,
				.buffer = _staticBLASBuffer,
				.offset = runningOffset,
				.size = buildSizesInfo[i].accelerationStructureSize,
				.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			};
			VK_CHECK(vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &blas));
			_bottomLevelAccelerationStructures.push_back(blas);

			buildInfos[i].dstAccelerationStructure = blas;
			runningOffset += blasOffsets[i];
		}

		Buffer		 scratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
		DeviceMemory scratchMemory;
		scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, scratchBufferSize);
		scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
		size_t	   offset = 0;
		const auto scratchBufferAddr = scratchBuffer.getDeviceAddress();
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			buildInfos[i].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			offset += buildSizesInfo[i].buildScratchSize;
			assert(buildInfos[i].geometryCount == 1); // See below! (pRangeInfos will be wrong in this case)
		}
		for(auto& rangeInfo : rangeInfos)
			pRangeInfos.push_back(&rangeInfo); // FIXME: Only work because geometryCount is always 1 here.

		// Build all the bottom acceleration structure on the device via a one-time command buffer submission
		immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
			// Build all BLAS in a single call. Note: This might cause sync. issues if buffers are shared (We made sure the scratchBuffer is not.)
			vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(buildInfos.size()), buildInfos.data(), pRangeInfos.data());
		});
	}

	std::vector<VkAccelerationStructureInstanceKHR> _accStructInstances;

	QuickTimer qt("TLAS building");
	uint32_t   customIndex = 0;
	for(const auto& blas : _bottomLevelAccelerationStructures) {
		// Get the bottom acceleration structures' handle, which will be used during the top level acceleration build
		VkAccelerationStructureDeviceAddressInfoKHR BLASAddressInfo{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
			.accelerationStructure = blas,
		};
		auto BLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &BLASAddressInfo);

		_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
			.transform = transforms[customIndex],
			.instanceCustomIndex = submeshesIndices[customIndex],
			.mask = 0xFF,
			.instanceShaderBindingTableRecordOffset = 0,
			.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
			.accelerationStructureReference = BLASDeviceAddress,
		});
		++customIndex;
	}

	_accStructInstancesBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
									 _accStructInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
	_accStructInstancesMemory.allocate(_device, _accStructInstancesBuffer,
									   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	_accStructInstancesMemory.fill(_accStructInstances.data(), _accStructInstances.size());

	VkAccelerationStructureGeometryKHR TLASGeometry{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry =
			{
				.instances =
					{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
						.arrayOfPointers = VK_FALSE,
						.data = _accStructInstancesBuffer.getDeviceAddress(),
					},
			},
		.flags = 0,
	};

	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
	};

	const uint32_t							 TBLAPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TLASBuildGeometryInfo, &TBLAPrimitiveCount, &TLASBuildSizesInfo);

	_tlasBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, TLASBuildSizesInfo.accelerationStructureSize);
	_tlasMemory.allocate(_device, _tlasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = _tlasBuffer,
		.size = TLASBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};

	VK_CHECK(vkCreateAccelerationStructureKHR(_device, &TLASCreateInfo, nullptr, &_topLevelAccelerationStructure));

	Buffer		 scratchBuffer;
	DeviceMemory scratchMemory;
	scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, TLASBuildSizesInfo.buildScratchSize);
	scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	TLASBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	TLASBuildGeometryInfo.dstAccelerationStructure = _topLevelAccelerationStructure;
	TLASBuildGeometryInfo.scratchData = {.deviceAddress = scratchBuffer.getDeviceAddress()};

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TBLAPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	immediateSubmitCompute([&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

void Application::createRayTracingPipeline() {
	auto rayTracingPipelineProperties = _device.getPhysicalDevice().getRaytracingPipelineProperties();
	if(rayTracingPipelineProperties.maxRecursionDepth <= 2) {
		throw std::runtime_error("VkPhysicalDeviceRayTracingPropertiesNV.maxRayRecursionDepth should be at least 3 for this pipeline.");
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

void Application::createRaytracingDescriptorSets() {
	assert(_topLevelAccelerationStructure != VK_NULL_HANDLE);
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		layoutsToAllocate.push_back(_rayTracingDescriptorSetLayout);
	_rayTracingDescriptorPool.create(_device, static_cast<uint32_t>(layoutsToAllocate.size()),
									 std::array<VkDescriptorPoolSize, 5>{
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
									 });
	_rayTracingDescriptorPool.allocate(layoutsToAllocate);

	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		auto writer =
			baseSceneWriter(_device, _rayTracingDescriptorPool.getDescriptorSets()[i], _scene, _topLevelAccelerationStructure, _irradianceProbes, _lightUniformBuffers[i]);
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

void Application::recordRayTracingCommands() {
	VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for(size_t i = 0; i < _rayTraceCommandBuffers.getBuffers().size(); i++) {
		auto& cmdBuff = _rayTraceCommandBuffers.getBuffers()[i];
		cmdBuff.begin();

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

		VK_CHECK(vkEndCommandBuffer(cmdBuff));
	}
}
