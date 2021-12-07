#include "Application.hpp"

void Application::createStorageImage() {
	_rayTraceStorageImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
								 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_rayTraceStorageImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_rayTraceStorageImageView.create(_device, VkImageViewCreateInfo{
												  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
												  .image = _rayTraceStorageImage,
												  .viewType = VK_IMAGE_VIEW_TYPE_2D,
												  .format = VK_FORMAT_B8G8R8A8_UNORM,
												  .subresourceRange =
													  VkImageSubresourceRange{
														  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
														  .baseMipLevel = 0,
														  .levelCount = 1,
														  .baseArrayLayer = 0,
														  .layerCount = 1,
													  },
											  });

	_rayTraceStorageImage.transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
										   VK_IMAGE_LAYOUT_GENERAL);
}

void Application::createAccelerationStructure() {
	VkFormatProperties2 formatProperties{
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	vkGetPhysicalDeviceFormatProperties2(_physicalDevice, VK_FORMAT_R32G32B32_SFLOAT, &formatProperties);
	assert(formatProperties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR);

	VkTransformMatrixKHR rootTransformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
	const size_t		 maxTransforms = 1024; // FIXME.
	size_t				 offsetInTransformBuffer = 0;
	_accStructTransformBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
									 maxTransforms * sizeof(VkTransformMatrixKHR));
	_accStructTransformMemory.allocate(_device, _accStructTransformBuffer,
									   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	_accStructTransformMemory.fill(&rootTransformMatrix, 1);
	++offsetInTransformBuffer;

	size_t submeshesCount = 0;
	for(const auto& m : _scene.getMeshes())
		submeshesCount += m.SubMeshes.size();

	std::vector<uint32_t>							submeshesIndices;
	std::vector<VkAccelerationStructureGeometryKHR> geometries;
	geometries.reserve(submeshesCount); // Avoid reallocation since buildInfos will refer to this.
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 rangeInfos;
	rangeInfos.reserve(submeshesCount); // Avoid reallocation since pRangeInfos will refer to this.
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos;
	std::vector<size_t>									   scratchBufferSizes;
	size_t												   scratchBufferSize = 0;

	const auto&												meshes = _scene.getMeshes();
	const std::function<void(const glTF::Node&, glm::mat4)> visitNode = [&](const glTF::Node& n, glm::mat4 transform) {
		transform = transform * n.transform;
		for(const auto& c : n.children) {
			visitNode(_scene.getNodes()[c], transform);
		}

		// This is a leaf
		if(n.mesh != -1) {
			for(size_t i = 0; i < meshes[n.mesh].SubMeshes.size(); ++i) {
				submeshesIndices.push_back(n.mesh + i);
				transform = glm::transpose(transform); // glm matrices are column-major, VkTransformMatrixKHR is row-major
				_accStructTransformMemory.fill(reinterpret_cast<VkTransformMatrixKHR*>(&transform), 1, offsetInTransformBuffer);
				VkAccelerationStructureKHR blas;
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
									.transformData = _accStructTransformBuffer.getDeviceAddress() + offsetInTransformBuffer * sizeof(VkTransformMatrixKHR),
								},
						},
					.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				});
				++offsetInTransformBuffer;

				VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
					.pNext = VK_NULL_HANDLE,
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
					.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
					.geometryCount = 1,
					.pGeometries = &geometries.back(),
					.ppGeometries = nullptr,
				};

				const uint32_t primitiveCount = static_cast<uint32_t>(meshes[n.mesh].SubMeshes[i].getIndices().size() / 3);

				VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
				vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
														&accelerationStructureBuildSizesInfo);

				_blasBuffers.emplace_back();
				auto& blasBuffer = _blasBuffers.back();
				_blasMemories.emplace_back();
				auto& blasMemory = _blasMemories.back();
				blasBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, accelerationStructureBuildSizesInfo.accelerationStructureSize);
				blasMemory.allocate(_device, blasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

				// Create the acceleration structure
				VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
					.pNext = VK_NULL_HANDLE,
					.buffer = blasBuffer,
					.size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
				};
				VK_CHECK(vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &blas));
				_bottomLevelAccelerationStructures.push_back(blas);

				// Complete the build infos.
				accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				accelerationBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
				accelerationBuildGeometryInfo.dstAccelerationStructure = blas;
				scratchBufferSizes.push_back(accelerationStructureBuildSizesInfo.buildScratchSize);
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
	visitNode(_scene.getRoot(), glm::mat4(1.0f));
	{
		{
			QuickTimer	 qt("BLAS building");
			Buffer		 scratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
			DeviceMemory scratchMemory;
			scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, scratchBufferSize);
			scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
			size_t offset = 0;
			for(size_t i = 0; i < buildInfos.size(); ++i) {
				buildInfos[i].scratchData = {.deviceAddress = scratchBuffer.getDeviceAddress() + offset};
				offset += scratchBufferSizes[i];
				assert(buildInfos[i].geometryCount == 1); // See below! (pRangeInfos will be wrong in this case)
			}
			for(auto& rangeInfo : rangeInfos)
				pRangeInfos.push_back(&rangeInfo); // FIXME: Only work because geometryCount is always 1 here.

			// Build all the bottom acceleration structure on the device via a one-time command buffer submission
			immediateSubmit([&](const CommandBuffer& commandBuffer) {
				// Build all BLAS in a single call. Note: This might cause sync. issues if buffers are shared (We made sure the scratchBuffer is not.)
				vkCmdBuildAccelerationStructuresKHR(commandBuffer, buildInfos.size(), buildInfos.data(), pRangeInfos.data());
			});
		}
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
			.transform = rootTransformMatrix,
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
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
	};

	const uint32_t							 TBLAPrimitiveCount = _accStructInstances.size();
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

	immediateSubmit([&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

void Application::createRayTracingPipeline() {
	uint32_t				   texturesCount = Textures.size();
	DescriptorSetLayoutBuilder dslBuilder;
	// Slot for binding top level acceleration structures to the ray generation shader
	dslBuilder.add(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, texturesCount)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 1);

	_rayTracingDescriptorSetLayout = dslBuilder.build(_device);

	VkPipelineLayoutCreateInfo pipeline_layout_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &_rayTracingDescriptorSetLayout.getHandle(),
	};

	_rayTracingPipelineLayout.create(_device, pipeline_layout_create_info);

	/*
		Setup ray tracing shader groups
		Each shader group points at the corresponding shader in the pipeline
	*/
	std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

	Shader raygenShader(_device, "./shaders_spv/raygen.rgen.spv");
	shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
	Shader raymissShader(_device, "./shaders_spv/miss.rmiss.spv");
	shader_stages.push_back(raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader raymissShadowShader(_device, "./shaders_spv/shadow.rmiss.spv");
	shader_stages.push_back(raymissShadowShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader closesthitShader(_device, "./shaders_spv/closesthit.rchit.spv");
	shader_stages.push_back(closesthitShader.getStageCreateInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
	// Ray generation group
	{
		VkRayTracingShaderGroupCreateInfoKHR raygen_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 0,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(raygen_group_ci);
	}

	// Ray miss group
	{
		VkRayTracingShaderGroupCreateInfoKHR miss_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 1,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(miss_group_ci);
	}
	{
		VkRayTracingShaderGroupCreateInfoKHR shadow_miss_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 2,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(shadow_miss_group_ci);
	}

	// Ray closest hit group
	{
		VkRayTracingShaderGroupCreateInfoKHR closes_hit_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = 3,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(closes_hit_group_ci);
	}

	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shader_stages.size()),
		.pStages = shader_stages.data(),
		.groupCount = static_cast<uint32_t>(shader_groups.size()),
		.pGroups = shader_groups.data(),
		.maxPipelineRayRecursionDepth = 2,
		.layout = _rayTracingPipelineLayout,
	};
	_rayTracingPipeline.create(_device, raytracing_pipeline_create_info, _pipelineCache);
}

void Application::createRaytracingDescriptorSets() {
	assert(_topLevelAccelerationStructure != VK_NULL_HANDLE);
	_rayTracingDescriptorPool.create(_device, 1,
									 std::array<VkDescriptorPoolSize, 5>{
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
									 });
	_rayTracingDescriptorPool.allocate({_rayTracingDescriptorSetLayout.getHandle()});

	// Setup the descriptor for binding our top level acceleration structure to the ray tracing shaders
	VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &_topLevelAccelerationStructure,
	};

	VkWriteDescriptorSet acceleration_structure_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &descriptor_acceleration_structure_info, // The acceleration structure descriptor has to be chained via pNext
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};

	VkDescriptorImageInfo image_descriptor{
		.imageView = _rayTraceStorageImageView,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkDescriptorBufferInfo buffer_descriptor{
		.buffer = _cameraUniformBuffers[0],
		.offset = 0,
		.range = sizeof(CameraBuffer),
	};

	// Bind all textures used in the scene.
	std::vector<VkDescriptorImageInfo> textureInfos;
	for(const auto& texture : Textures) {
		textureInfos.push_back(VkDescriptorImageInfo{
			.sampler = *texture.sampler,
			.imageView = texture.gpuImage->imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	}

	VkDescriptorBufferInfo verticesInfos{
		.buffer = Mesh::VertexBuffer,
		.offset = 0,
		.range = Mesh::NextVertexMemoryOffset,
	};

	VkDescriptorBufferInfo indicesInfos{
		.buffer = Mesh::IndexBuffer,
		.offset = 0,
		.range = Mesh::NextIndexMemoryOffset,
	};

	VkDescriptorBufferInfo offsetsInfos{
		.buffer = Mesh::OffsetTableBuffer,
		.offset = 0,
		.range = Mesh::OffsetTableSize,
	};

	VkDescriptorBufferInfo materialsInfos{
		.buffer = MaterialBuffer,
		.offset = 0,
		.range = sizeof(Material::GPUData) * Materials.size(),
	};

	VkWriteDescriptorSet result_image_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.pImageInfo = &image_descriptor,
	};

	VkWriteDescriptorSet uniform_buffer_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_descriptor,
	};

	VkWriteDescriptorSet textures_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 3,
		.descriptorCount = static_cast<uint32_t>(textureInfos.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = textureInfos.data(),
	};

	VkWriteDescriptorSet vertices_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 4,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &verticesInfos,
	};

	VkWriteDescriptorSet indices_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 5,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &indicesInfos,
	};

	VkWriteDescriptorSet offsets_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 6,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &offsetsInfos,
	};

	VkWriteDescriptorSet materials_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 7,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &materialsInfos,
	};

	std::vector<VkWriteDescriptorSet> write_descriptor_sets = {
		acceleration_structure_write, result_image_write, uniform_buffer_write, textures_write, vertices_write, indices_write, offsets_write, materials_write};
	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, VK_NULL_HANDLE);
}

inline uint32_t aligned_size(uint32_t value, uint32_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

void Application::recordRayTracingCommands() {
	VkPhysicalDeviceRayTracingPropertiesNV rayTracingPipelineProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2			   deviceProperties{
				   .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
				   .pNext = &rayTracingPipelineProperties,
	   };
	vkGetPhysicalDeviceProperties2(_device.getPhysicalDevice(), &deviceProperties);
	if(rayTracingPipelineProperties.maxRecursionDepth <= 1) {
		throw std::runtime_error("VkPhysicalDeviceRayTracingPropertiesNV.maxRayRecursionDepth should be at least 2.");
	}

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
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(_device, _rayTracingPipeline, 0, totalEntries, stb_size, shader_handle_storage.data()));

	size_t offsetInShaderHandleStorage = 0;
	for(size_t i = 0; i < _rayShaderBindingTablesCount; ++i) {
		if(!_rayTracingShaderBindingTables[i]) {
			_rayTracingShaderBindingTables[i].create(
				_device, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, regionSizes[i]);
			_rayTracingShaderBindingTablesMemory[i].allocate(_device, _rayTracingShaderBindingTables[i],
															 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		char* mapped = (char*)_rayTracingShaderBindingTablesMemory[i].map(regionSizes[i]);
		for(size_t handleIdx = 0; handleIdx < entriesCount[i]; ++handleIdx) {
			memcpy(mapped + handleIdx * handle_size_aligned, shader_handle_storage.data() + offsetInShaderHandleStorage + handleIdx * handle_size, handle_size);
		}
		_rayTracingShaderBindingTablesMemory[i].unmap();
		offsetInShaderHandleStorage += entriesCount[i] * handle_size;
	}

	VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for(size_t i = 0; i < _rayTraceCommandBuffers.getBuffers().size(); i++) {
		auto& cmdBuff = _rayTraceCommandBuffers.getBuffers()[i];
		cmdBuff.begin();

		/*
			Setup the strided device address regions pointing at the shader identifiers in the shader binding table
		*/

		VkStridedDeviceAddressRegionKHR raygen_shader_sbt_entry{};
		raygen_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[0].getDeviceAddress();
		raygen_shader_sbt_entry.stride = handle_size_aligned;
		raygen_shader_sbt_entry.size = regionSizes[0];

		VkStridedDeviceAddressRegionKHR miss_shader_sbt_entry{};
		miss_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[1].getDeviceAddress();
		miss_shader_sbt_entry.stride = handle_size_aligned;
		miss_shader_sbt_entry.size = regionSizes[1];

		VkStridedDeviceAddressRegionKHR hit_shader_sbt_entry{};
		hit_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[2].getDeviceAddress();
		hit_shader_sbt_entry.stride = handle_size_aligned;
		hit_shader_sbt_entry.size = regionSizes[2];

		VkStridedDeviceAddressRegionKHR callable_shader_sbt_entry{};

		/*
			Dispatch the ray tracing commands
		*/
		vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipeline);
		vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipelineLayout, 0, 1, &_rayTracingDescriptorPool.getDescriptorSets()[0], 0, 0);

		vkCmdTraceRaysKHR(cmdBuff, &raygen_shader_sbt_entry, &miss_shader_sbt_entry, &hit_shader_sbt_entry, &callable_shader_sbt_entry, _width, _height, 1);

		/*
			Copy ray tracing output to swap chain image
		*/

		// Prepare current swap chain image as transfer destination
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);
		// Prepare ray tracing output image as transfer source
		Image::setLayout(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource_range);

		VkImageCopy copy_region{
			.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.srcOffset = {0, 0, 0},
			.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {_width, _height, 1},
		};
		vkCmdCopyImage(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// Transition swap chain image back to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to prepare for UI rendering
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, subresource_range);
		// Transition ray tracing output image back to general layout
		Image::setLayout(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresource_range);

		VK_CHECK(vkEndCommandBuffer(cmdBuff));
	}
}
