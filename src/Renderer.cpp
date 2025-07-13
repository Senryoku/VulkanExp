#include "Renderer.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <QuickTimer.hpp>
#include <vulkan/Shader.hpp>

void Renderer::free() {
	destroyAccelerationStructures();
	destroyVertexSkinningPipeline();
	freeMeshesDeviceMemory();
	stagingBuffer.free();
}

void Renderer::freeMeshesDeviceMemory() {
	OffsetTable.free();
	Vertices.free();
	Indices.free();
	Joints.free();
	Weights.free();
	MotionVectors.free();

	StaticOffsetTableSizeInBytes = 0;
	StaticVertexBufferSizeInBytes = 0;
	StaticJointsBufferSizeInBytes = 0;
	StaticWeightsBufferSizeInBytes = 0;

	_updateQueryPools.clear();
}

void Renderer::destroyAccelerationStructures() {
	destroyTLAS();

	_bottomLevelAccelerationStructures.clear();
	_blasMemory.free();
	_blasScratchBuffer.destroy();
	_blasScratchMemory.free();
}

void Renderer::destroyTLAS() {
	vkDestroyAccelerationStructureKHR(*_device, _topLevelAccelerationStructure, nullptr);
	_topLevelAccelerationStructure = VK_NULL_HANDLE;
	_tlasBuffer.destroy();
	_tlasMemory.free();
	_instancesBuffer.destroy();
	_previousInstancesBuffer.destroy();
	_instancesMemory.free();
	_accStructInstances.clear();
	_accStructInstancesBuffer.destroy();
	_accStructInstancesMemory.free();
	_tlasScratchBuffer.destroy();
	_tlasScratchMemory.free();
}

void Renderer::allocateMeshes() {
	if(Vertices)
		freeMeshesDeviceMemory();

	updateMeshOffsetTable();
	auto indexMemoryTypeBits = getMeshes()[0].getIndexBuffer().getMemoryRequirements().memoryTypeBits;
	Vertices.init(*_device,
				  VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, StaticVertexBufferSizeInBytes + MaxSkinnedVertexSizeInBytes, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT );
	Indices.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, StaticIndexBufferSizeInBytes,
				 VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
	for(const auto& mesh : getMeshes()) {
		if(!mesh.isValid() && !mesh.dynamic)
			continue;

		Vertices.bind(mesh.getVertexBuffer());
		Indices.bind(mesh.getIndexBuffer());

		if(mesh.isSkinned()) {
			if(!Joints) {
				Joints.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, StaticJointsBufferSizeInBytes);
				Weights.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, StaticWeightsBufferSizeInBytes);
			}
			Joints.bind(mesh.getSkinJointsBuffer());
			Weights.bind(mesh.getSkinWeightsBuffer());
		}
	}

	MotionVectors.init(*_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(glm::vec4) * MaxSkinnedVertexSizeInBytes / sizeof(Vertex), VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

	OffsetTable.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, StaticOffsetTableSizeInBytes + 1024 * sizeof(OffsetEntry), VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

	uploadMeshOffsetTable();

	allocateSkinnedMeshes();
}

void Renderer::updateMeshOffsetTable() {
	size_t totalVertexSize = 0;
	size_t totalIndexSize = 0;
	StaticJointsBufferSizeInBytes = 0;
	StaticWeightsBufferSizeInBytes = 0;
	_offsetTable.clear();
	for(auto& m : getMeshes()) {
		if(!m.isValid() && !m.dynamic)
			continue;

		auto vertexBufferMemReq = m.getVertexBuffer().getMemoryRequirements();
		auto indexBufferMemReq = m.getIndexBuffer().getMemoryRequirements();
		m.indexIntoOffsetTable = static_cast<uint32_t>(_offsetTable.size());
		_offsetTable.push_back(OffsetEntry{
			static_cast<uint32_t>(m.defaultMaterialIndex),
			static_cast<uint32_t>(totalVertexSize / sizeof(Vertex)),
			static_cast<uint32_t>(totalIndexSize / sizeof(uint32_t)),
		});
		totalVertexSize += vertexBufferMemReq.size;
		totalIndexSize += indexBufferMemReq.size;

		if(m.isSkinned()) {
			StaticJointsBufferSizeInBytes += m.getSkinJointsBuffer().getMemoryRequirements().size;
			StaticWeightsBufferSizeInBytes += m.getSkinWeightsBuffer().getMemoryRequirements().size;
		}
	}
	StaticVertexBufferSizeInBytes = totalVertexSize;
	StaticIndexBufferSizeInBytes = totalIndexSize;
	StaticOffsetTableSizeInBytes = static_cast<uint32_t>(sizeof(OffsetEntry) * _offsetTable.size());
}

void Renderer::uploadMeshOffsetTable() {
	if(!_offsetTable.empty())
		copyViaStagingBuffer(OffsetTable.buffer(), _offsetTable);
}

void Renderer::allocateSkinnedMeshes() {
	sortRenderers();
	updateSkinnedMeshOffsetTable();
	uploadSkinnedMeshOffsetTable();

	_updateQueryPools.clear();
	_updateQueryPools.resize(2);
	for(auto& query : _updateQueryPools)
		query.create(*_device, VK_QUERY_TYPE_TIMESTAMP, 2);
}

void Renderer::updateSkinnedMeshOffsetTable() {
	auto   instances = _scene->getRegistry().view<SkinnedMeshRendererComponent>();
	size_t totalVertexSize = 0;
	size_t idx = 0;
	_skinnedOffsetTable.clear();
	for(auto& entity : instances) {
		auto& skinnedMeshRenderer = _scene->getRegistry().get<SkinnedMeshRendererComponent>(entity);
		auto  vertexBufferMemReq = getMeshes()[skinnedMeshRenderer.meshIndex].getVertexBuffer().getMemoryRequirements();
		skinnedMeshRenderer.indexIntoOffsetTable = static_cast<uint32_t>(_offsetTable.size() + _skinnedOffsetTable.size());
		_skinnedOffsetTable.push_back(OffsetEntry{
			static_cast<uint32_t>(skinnedMeshRenderer.materialIndex),
			static_cast<uint32_t>((StaticVertexBufferSizeInBytes + totalVertexSize) / sizeof(Vertex)),
			static_cast<uint32_t>(_offsetTable[getMeshes()[skinnedMeshRenderer.meshIndex].indexIntoOffsetTable].indexOffset),
		});
		totalVertexSize += vertexBufferMemReq.size;
	}
	assert(totalVertexSize < MaxSkinnedVertexSizeInBytes);
	assert(_skinnedOffsetTable.size() < 1024);

	SkinnedOffsetTableSizeInBytes = static_cast<uint32_t>(sizeof(OffsetEntry) * _skinnedOffsetTable.size());
}

void Renderer::uploadSkinnedMeshOffsetTable() {
	if(_skinnedOffsetTable.size() > 0)
		copyViaStagingBuffer(OffsetTable.buffer(), _skinnedOffsetTable, 0, StaticOffsetTableSizeInBytes);
}

bool Renderer::updateAnimations(float deltaTime) {
	bool changes = false;
	// TODO: Morph (i.e. weights animation)
	auto animatedNodes = _scene->getRegistry().view<AnimationComponent>();
	for(auto& entity : animatedNodes) {
		auto& animationComponent = _scene->getRegistry().get<AnimationComponent>(entity);
		if(animationComponent.running)
			animationComponent.time += deltaTime;
		if(animationComponent.running || animationComponent.forceUpdate) {
			animationComponent.forceUpdate = false;
			if(animationComponent.animationIndex != InvalidAnimationIndex)
				for(const auto& n : Animations[animationComponent.animationIndex].nodeAnimations) {
					auto pose = n.second.at(animationComponent.time);
					// Use this pose transform in the hierarchy
					_scene->markDirty(n.first);
					_scene->getRegistry().get<NodeComponent>(n.first).transform = pose.transform;
					changes = true;
				}
		}
	}
	return changes;
}

struct VertexSkinningPushConstant {
	uint32_t srcOffset = 0;
	uint32_t dstOffset = 0;
	uint32_t size = 0;
	uint32_t motionVectorsOffset = 0;
};

bool Renderer::updateSkinnedVertexBuffer() {
	auto instances = _scene->getRegistry().view<SkinnedMeshRendererComponent>();
	for(auto& entity : instances) {
		const auto& skinnedMeshRenderer = _scene->getRegistry().get<SkinnedMeshRendererComponent>(entity);
		const auto& skin = _scene->getSkins()[skinnedMeshRenderer.skinIndex];

		std::vector<glm::mat4> jointPoses;
		if(skin.joints.size() > MaxJoints) {
			warn("Renderer::updateSkinnedVertexBuffer: Too many joints (skin.joints.size() ({}) > MaxJoints ({}))\n", skin.joints.size(), MaxJoints);
			continue;
		}
		jointPoses.resize(skin.joints.size());
		// FIXME: Not sure what inverseGlobalTransform should be.
		const auto		parent = _scene->getRegistry().get<NodeComponent>(entity).parent;
		const glm::mat4 inverseGlobalTransform = parent == entt::null ? glm::mat4(1.0f) : glm::inverse(_scene->getRegistry().get<NodeComponent>(parent).globalTransform);
		// FIXME: This is extremely slow.
		for(auto i = 0; i < skin.joints.size(); ++i)
			jointPoses[i] = (inverseGlobalTransform * _scene->getRegistry().get<NodeComponent>(skin.joints[i]).globalTransform * skin.inverseBindMatrices[i]);
		_currentJoints.memory().fill(jointPoses.data(), jointPoses.size());
		writeSkinningDescriptorSet(skinnedMeshRenderer);

		_device->immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
			_vertexSkinningPipeline.bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _vertexSkinningPipeline.getLayout(), 0, 1, &_vertexSkinningDescriptorPool.getDescriptorSets()[0],
									0, 0);
			VertexSkinningPushConstant constants{
				.srcOffset = _offsetTable[_scene->getMeshes()[skinnedMeshRenderer.meshIndex].indexIntoOffsetTable].vertexOffset,
				.dstOffset = _skinnedOffsetTable[skinnedMeshRenderer.indexIntoOffsetTable - StaticOffsetTableSizeInBytes / sizeof(OffsetEntry)].vertexOffset,
				.size = static_cast<uint32_t>(_scene->getMeshes()[skinnedMeshRenderer.meshIndex].getVertices().size()),
				.motionVectorsOffset = _skinnedOffsetTable[skinnedMeshRenderer.indexIntoOffsetTable - StaticOffsetTableSizeInBytes / sizeof(OffsetEntry)].vertexOffset -
									   static_cast<uint32_t>(StaticVertexBufferSizeInBytes / sizeof(Vertex)),
			};
			vkCmdPushConstants(commandBuffer, _vertexSkinningPipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VertexSkinningPushConstant), &constants);
			vkCmdDispatch(commandBuffer, std::ceil(constants.size / 128.0), 1, 1);
		});
	}
	return true;
}

constexpr VkAccelerationStructureGeometryKHR BaseVkAccelerationStructureGeometryKHR{
	.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
	.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
	.geometry =
		VkAccelerationStructureGeometryDataKHR{
			.triangles =
				VkAccelerationStructureGeometryTrianglesDataKHR{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
					.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
					.vertexData = 0,
					.vertexStride = sizeof(Vertex),
					.maxVertex = 0,
					.indexType = VK_INDEX_TYPE_UINT32,
					.indexData = 0,
					.transformData = 0,
				},
		},
	.flags = 0,
};

constexpr VkAccelerationStructureBuildGeometryInfoKHR BaseAccelerationStructureBuildGeometryInfo{
	.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
	.pNext = VK_NULL_HANDLE,
	.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
	.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
	.srcAccelerationStructure = VK_NULL_HANDLE,
	.geometryCount = 1,
	.pGeometries = nullptr,
	.ppGeometries = nullptr,
};

void Renderer::createAccelerationStructures() {
	if(_topLevelAccelerationStructure) {
		destroyAccelerationStructures();
	}

	VkFormatProperties2 formatProperties{
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	vkGetPhysicalDeviceFormatProperties2(_device->getPhysicalDevice(), VK_FORMAT_R32G32B32_SFLOAT, &formatProperties);
	assert(formatProperties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR);

	VkTransformMatrixKHR rootTransformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

	size_t meshesCount = getMeshes().size() + MaxSkinnedBLAS; // FIXME: Reserve MaxSkinnedBLAS skinned instances

	std::vector<VkAccelerationStructureGeometryKHR>			 geometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 rangeInfos;
	std::vector<size_t>										 scratchBufferSizes;
	size_t													 scratchBufferSize = 0;
	std::vector<uint32_t>									 blasOffsets; // Start of each BLAS in buffer (aligned to 256 bytes)
	size_t													 totalBLASSize = 0;
	std::vector<VkAccelerationStructureBuildSizesInfoKHR>	 buildSizesInfo;
	geometries.reserve(meshesCount); // Avoid reallocation since buildInfos will refer to this.
	rangeInfos.reserve(meshesCount); // Avoid reallocation since pRangeInfos will refer to this.
	blasOffsets.reserve(meshesCount);
	buildSizesInfo.reserve(meshesCount);
	_skinnedBLASGeometries.clear();
	_skinnedBLASGeometries.reserve(MaxSkinnedBLAS);
	_skinnedBLASBuildGeometryInfos.clear();
	_skinnedBLASBuildGeometryInfos.reserve(MaxSkinnedBLAS);
	_skinnedBLASBuildRangeInfos.clear();
	_skinnedBLASBuildRangeInfos.reserve(MaxSkinnedBLAS);

	auto baseGeometry = BaseVkAccelerationStructureGeometryKHR;
	auto accelerationBuildGeometryInfo = BaseAccelerationStructureBuildGeometryInfo;

	auto& meshes = getMeshes();
	{
		/* Notes:
		 * Right now there's a one-to-one relation between meshes and geometries.
		 * This is not garanteed to be optimal (Apparently less BLAS is better, i.e. grouping geometries), but we don't have a mechanism to
		 * retrieve data for distinct geometries (vertices/indices/material) in our ray tracing shaders yet.
		 * This should be doable using the gl_GeometryIndexEXT built-in.
		 *
		 * BLASMemory:
		 *  [Used Static BLASes][Reserved Memory for currently invalid (non-skinned) dynamic meshes][BLASes for Skinned Meshes]
		 */
		QuickTimer qt("BLAS building");
		// Collect all submeshes and query the memory requirements
		size_t createdStaticBLASCount = 0;
		for(auto& mesh : meshes) {
			// Reserve some additional memory for dynamic meshes even if they're empty for now.
			// We won't create/build them for now.
			if(!mesh.isValid() && !mesh.dynamic)
				continue;

			baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{mesh.getVertexBuffer().getDeviceAddress()};
			baseGeometry.geometry.triangles.maxVertex = mesh.dynamic ? mesh.DynamicVertexCapacity : static_cast<uint32_t>(mesh.getVertices().size() - 1);
			baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
			accelerationBuildGeometryInfo.pGeometries = &baseGeometry;

			accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			if(mesh.dynamic)
				accelerationBuildGeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

			const uint32_t primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3);
			auto		   accelerationStructureBuildSizesInfo = AccelerationStructure::getBuildSize(*_device, accelerationBuildGeometryInfo, primitiveCount);
			// FIXME: Query this 256 alignment instead of hardcoding it.
			uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
			totalBLASSize += alignedSize;
			scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

			if(!mesh.isValid())
				continue;

			geometries.push_back(baseGeometry);
			accelerationBuildGeometryInfo.pGeometries = &geometries.back();

			buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);
			blasOffsets.push_back(alignedSize);
			buildInfos.push_back(accelerationBuildGeometryInfo);
			rangeInfos.push_back({
				.primitiveCount = primitiveCount,
				.primitiveOffset = 0,
				.firstVertex = 0,
				.transformOffset = 0,
			});
			mesh.blasIndex = buildSizesInfo.size() - 1;
			++createdStaticBLASCount;
		}

		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		// Create an additional BLAS for each skinned mesh instance.
		auto instances = _scene->getRegistry().view<SkinnedMeshRendererComponent>();
		for(auto& entity : instances) {
			auto& skinnedMeshRenderer = _scene->getRegistry().get<SkinnedMeshRendererComponent>(entity);
			auto& mesh = getMeshes()[skinnedMeshRenderer.meshIndex];
			if(mesh.dynamic)
				warn("Mesh '{}' marked are dynamic AND used as a skinned mesh (Is this really a valid use case?).\n", mesh.name);

			skinnedMeshRenderer.blasIndex = buildInfos.size();

			baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{
				Vertices.buffer().getDeviceAddress() +
				sizeof(Vertex) * _skinnedOffsetTable[skinnedMeshRenderer.indexIntoOffsetTable - StaticOffsetTableSizeInBytes / sizeof(OffsetEntry)].vertexOffset};
			baseGeometry.geometry.triangles.maxVertex = static_cast<uint32_t>(mesh.getVertices().size() - 1);
			baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
			geometries.push_back(baseGeometry);

			accelerationBuildGeometryInfo.pGeometries = &geometries.back();

			const uint32_t primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3);

			auto accelerationStructureBuildSizesInfo = AccelerationStructure::getBuildSize(*_device, accelerationBuildGeometryInfo, primitiveCount);

			// FIXME: Query this 256 alignment instead of hardcoding it.
			uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
			totalBLASSize += alignedSize;
			scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

			buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);
			blasOffsets.push_back(alignedSize);
			buildInfos.push_back(accelerationBuildGeometryInfo);
			rangeInfos.push_back({
				.primitiveCount = primitiveCount,
				.primitiveOffset = 0,
				.firstVertex = 0,
				.transformOffset = 0,
			});

			// Also keep a copy for later re-builds
			_skinnedBLASGeometries.push_back(baseGeometry);
			accelerationBuildGeometryInfo.pGeometries = &_skinnedBLASGeometries.back();
			_skinnedBLASBuildGeometryInfos.push_back(accelerationBuildGeometryInfo);
			_skinnedBLASBuildRangeInfos.push_back(rangeInfos.back());
		}
		_blasMemory.init(*_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						 totalBLASSize, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
		size_t runningOffset = 0;
		_bottomLevelAccelerationStructures.resize(buildInfos.size());
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			// Create the acceleration structure
			_bottomLevelAccelerationStructures[i].create(*_device, _blasMemory.buffer(), runningOffset, buildSizesInfo[i].accelerationStructureSize);

			buildInfos[i].dstAccelerationStructure = _bottomLevelAccelerationStructures[i];
			if(i >= createdStaticBLASCount)
				_skinnedBLASBuildGeometryInfos[i - createdStaticBLASCount].dstAccelerationStructure = _bottomLevelAccelerationStructures[i];
			runningOffset += blasOffsets[i];
		}
		_blasMemory.reserve(runningOffset);

		_blasScratchBuffer.create(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, scratchBufferSize);
		_blasScratchMemory.allocate(*_device, _blasScratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
		size_t	   offset = 0;
		const auto scratchBufferAddr = _blasScratchBuffer.getDeviceAddress();
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			buildInfos[i].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			if(i >= createdStaticBLASCount) {
				_skinnedBLASBuildGeometryInfos[i - createdStaticBLASCount].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			}
			offset += buildSizesInfo[i].buildScratchSize;
			assert(buildInfos[i].geometryCount == 1); // See below! (pRangeInfos will be wrong in this case)
		}

		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos;
		for(auto& rangeInfo : rangeInfos)
			pRangeInfos.push_back(&rangeInfo); // FIXME: Only works because geometryCount is always 1 here.

		// Build all the bottom acceleration structure on the device via a one-time command buffer submission
		_device->immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
			// Build all BLAS in a single call. Note: This might cause sync. issues if buffers are shared (We made sure the scratchBuffer is not.)
			vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(buildInfos.size()), buildInfos.data(), pRangeInfos.data());
		});
	}

	createTLAS();
}

void Renderer::createBLAS(MeshIndex idx) {
	auto& mesh = (*_scene)[idx];
	assert(mesh.blasIndex == -1);
	auto baseGeometry = BaseVkAccelerationStructureGeometryKHR;
	auto accelerationBuildGeometryInfo = BaseAccelerationStructureBuildGeometryInfo;
	baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{mesh.getVertexBuffer().getDeviceAddress()};
	baseGeometry.geometry.triangles.maxVertex = mesh.dynamic ? mesh.DynamicVertexCapacity : static_cast<uint32_t>(mesh.getVertices().size());
	baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
	accelerationBuildGeometryInfo.scratchData = {.deviceAddress = _blasScratchBuffer.getDeviceAddress()}; // Should not be run in parallel - I think - so probably fine...?
	accelerationBuildGeometryInfo.pGeometries = &baseGeometry;
	accelerationBuildGeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	auto										   primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3);
	const VkAccelerationStructureBuildRangeInfoKHR rangeInfo{
		.primitiveCount = primitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos{&rangeInfo};

	auto accelerationStructureBuildSizesInfo = AccelerationStructure::getBuildSize(*_device, accelerationBuildGeometryInfo, primitiveCount);

	_bottomLevelAccelerationStructures.emplace_back();
	_bottomLevelAccelerationStructures.back().create(*_device, _blasMemory.buffer(), _blasMemory.size(), accelerationStructureBuildSizesInfo.accelerationStructureSize);
	uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
	_blasMemory.reserve(alignedSize);

	mesh.blasIndex = _bottomLevelAccelerationStructures.size() - 1;
	accelerationBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	accelerationBuildGeometryInfo.dstAccelerationStructure = _bottomLevelAccelerationStructures[mesh.blasIndex];

	_device->immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo, &pRangeInfos); });
}

void Renderer::updateBLAS(MeshIndex idx) {
	const auto& mesh = (*_scene)[idx];
	assert(mesh.blasIndex != -1);
	auto baseGeometry = BaseVkAccelerationStructureGeometryKHR;
	auto accelerationBuildGeometryInfo = BaseAccelerationStructureBuildGeometryInfo;
	baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{mesh.getVertexBuffer().getDeviceAddress()};
	baseGeometry.geometry.triangles.maxVertex = mesh.dynamic ? mesh.DynamicVertexCapacity : static_cast<uint32_t>(mesh.getVertices().size());
	baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
	accelerationBuildGeometryInfo.scratchData = {.deviceAddress = _blasScratchBuffer.getDeviceAddress()}; // Should not be run in parallel - I think - so probably fine...?
	accelerationBuildGeometryInfo.srcAccelerationStructure = _bottomLevelAccelerationStructures[mesh.blasIndex];
	accelerationBuildGeometryInfo.dstAccelerationStructure = _bottomLevelAccelerationStructures[mesh.blasIndex];
	accelerationBuildGeometryInfo.pGeometries = &baseGeometry;
	accelerationBuildGeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	const VkAccelerationStructureBuildRangeInfoKHR rangeInfo{
		.primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3),
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos{&rangeInfo};
	_device->immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo, &pRangeInfos); });
}

void Renderer::sortRenderers() {
	_scene->getRegistry().sort<MeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
	_scene->getRegistry().sort<SkinnedMeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
}

void Renderer::createTLAS() {
	QuickTimer qt("TLAS building");

	sortRenderers();

	const auto& meshes = getMeshes();
	{
		auto instances = _scene->getRegistry().view<MeshRendererComponent>();
		for(auto& entity : instances) {
			const auto& meshRendererComponent = _scene->getRegistry().get<MeshRendererComponent>(entity);
			const auto& mesh = (*_scene)[meshRendererComponent.meshIndex];
			if(mesh.blasIndex == -1)
				continue;
			auto				 tmp = glm::transpose(_scene->getRegistry().get<NodeComponent>(entity).globalTransform);
			VkTransformMatrixKHR transposedTransform = *reinterpret_cast<VkTransformMatrixKHR*>(&tmp); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			// Get the bottom acceleration structures' handle, which will be used during the top level acceleration build
			auto BLASDeviceAddress = _bottomLevelAccelerationStructures[mesh.blasIndex].getDeviceAddress();

			_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
				.transform = transposedTransform,
				.instanceCustomIndex = meshes[meshRendererComponent.meshIndex].indexIntoOffsetTable,
				.mask = InstanceMask::Static,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
				.accelerationStructureReference = BLASDeviceAddress,
			});
		}
	}
	{
		auto instances = _scene->getRegistry().view<SkinnedMeshRendererComponent>();
		for(auto& entity : instances) {
			auto& skinnedMeshRendererComponent = _scene->getRegistry().get<SkinnedMeshRendererComponent>(entity);
			if(!(*_scene)[skinnedMeshRendererComponent.meshIndex].isValid())
				continue;
			auto				 tmp = glm::transpose(_scene->getRegistry().get<NodeComponent>(entity).globalTransform);
			VkTransformMatrixKHR transposedTransform = *reinterpret_cast<VkTransformMatrixKHR*>(&tmp); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			auto				 BLASDeviceAddress = _bottomLevelAccelerationStructures[skinnedMeshRendererComponent.blasIndex].getDeviceAddress();

			_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
				.transform = transposedTransform,
				.instanceCustomIndex = skinnedMeshRendererComponent.indexIntoOffsetTable,
				.mask = InstanceMask::Skinned,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
				.accelerationStructureReference = BLASDeviceAddress,
			});
		}
	}

	_accStructInstancesBuffer.create(
		*_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		_accStructInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
	_accStructInstancesMemory.allocate(*_device, _accStructInstancesBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
	copyViaStagingBuffer(_accStructInstancesBuffer, _accStructInstances);

	_instancesMemory.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						  2 * _accStructInstances.size() * sizeof(InstanceData), 0, 2);
	_instancesMemory.bind(_instancesBuffer, _accStructInstances.size() * sizeof(InstanceData));
	_instancesMemory.bind(_previousInstancesBuffer, _accStructInstances.size() * sizeof(InstanceData));
	updateTransforms();

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
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
	};

	const uint32_t							 TLASPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(*_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TLASBuildGeometryInfo, &TLASPrimitiveCount, &TLASBuildSizesInfo);

	_tlasBuffer.create(*_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, TLASBuildSizesInfo.accelerationStructureSize);
	_tlasMemory.allocate(*_device, _tlasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = _tlasBuffer,
		.size = TLASBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};

	VK_CHECK(vkCreateAccelerationStructureKHR(*_device, &TLASCreateInfo, nullptr, &_topLevelAccelerationStructure));

	_tlasScratchBuffer.create(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, TLASBuildSizesInfo.buildScratchSize);
	_tlasScratchMemory.allocate(*_device, _tlasScratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	TLASBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	TLASBuildGeometryInfo.dstAccelerationStructure = _topLevelAccelerationStructure;
	TLASBuildGeometryInfo.scratchData = {.deviceAddress = _tlasScratchBuffer.getDeviceAddress()};

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TLASPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	_device->immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

bool Renderer::updateSkinnedBLAS() {
	QuickTimer qt(_cpuBLASUpdateTimes);

	if(_skinnedBLASBuildGeometryInfos.empty())
		return false;

	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos;
	for(auto& rangeInfo : _skinnedBLASBuildRangeInfos)
		pRangeInfos.push_back(&rangeInfo);

	if(_updateQueryPools[0].newSampleFlag) {
		auto queryResults = _updateQueryPools[0].get();
		if(queryResults.size() >= 2 && queryResults[0].available && queryResults[1].available) {
			_skinnedBLASUpdateTimes.add(0.000001f * (queryResults[1].result - queryResults[0].result));
			_updateQueryPools[0].newSampleFlag = false;
		}
	}
	_device->immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
		_updateQueryPools[0].reset(commandBuffer);
		_updateQueryPools[0].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(_skinnedBLASBuildGeometryInfos.size()), _skinnedBLASBuildGeometryInfos.data(), pRangeInfos.data());
		_updateQueryPools[0].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
	}); // FIXME: Too much synchronisation here (WaitQueueIdle)
	_updateQueryPools[0].newSampleFlag = true;
	return true;
}

void Renderer::updateAccelerationStructureInstances() {
	if(_accStructInstances.size() < _instancesData.size())
		warn("_instancesData and _accStructInstances out of sync (sizes: {} and {})\n", _instancesData.size(), _accStructInstances.size());
	for(size_t i = 0; i < std::min(_instancesData.size(), _accStructInstances.size()); ++i) {
		auto t = glm::transpose(_instancesData[i].transform);
		_accStructInstances[i].transform = *reinterpret_cast<VkTransformMatrixKHR*>(&t);
	}
	copyViaStagingBuffer(_accStructInstancesBuffer, _accStructInstances);
}

void Renderer::updateTLAS() {
	// TODO: Optimise (including with regards to the GPU sync.).
	QuickTimer qt(_cpuTLASUpdateTimes);

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
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
	};
	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
		.srcAccelerationStructure = _topLevelAccelerationStructure,
		.dstAccelerationStructure = _topLevelAccelerationStructure,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
		.scratchData = {.deviceAddress = _tlasScratchBuffer.getDeviceAddress()},
	};
	const uint32_t							 TBLAPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TBLAPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	if(_updateQueryPools[1].newSampleFlag) {
		auto queryResults = _updateQueryPools[1].get();
		if(queryResults.size() >= 2 && queryResults[0].available && queryResults[1].available) {
			_tlasUpdateTimes.add(0.000001f * (queryResults[1].result - queryResults[0].result));
			_updateQueryPools[1].newSampleFlag = false;
		}
	}
	_device->immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
		_updateQueryPools[1].reset(commandBuffer);
		_updateQueryPools[1].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data());
		_updateQueryPools[1].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
	});
	_updateQueryPools[1].newSampleFlag = true;
}

void Renderer::onHierarchicalChanges(float deltaTime) {
	updateTransforms();
	updateAccelerationStructureInstances();
	auto vertexUpdate = updateSkinnedVertexBuffer();
	if(vertexUpdate)
		updateSkinnedBLAS();
	updateTLAS();
}

void Renderer::update() {
	if(!_instancesData.empty())
		_device->immediateSubmitTransfert([&](const CommandBuffer& cmdBuff) {
			VkBufferCopy copyRegion{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = _instancesData.size() * sizeof(InstanceData),
			};
			vkCmdCopyBuffer(cmdBuff, _instancesBuffer, _previousInstancesBuffer, 1, &copyRegion);
		});
}

void Renderer::updateTransforms() {
	sortRenderers();
	auto meshRenderers = _scene->getRegistry().view<MeshRendererComponent, NodeComponent>();
	auto skinnedMeshRenderers = _scene->getRegistry().view<SkinnedMeshRendererComponent, NodeComponent>();

	// TODO: Optimize by only updating dirtyNode when possible
	_instancesData.clear();
	_instancesData.reserve(meshRenderers.size_hint() + skinnedMeshRenderers.size_hint());
	for(auto&& [entity, meshRenderer, node] : meshRenderers.each())
		if(_scene->getMeshes()[meshRenderer.meshIndex].isValid())
			_instancesData.push_back({node.globalTransform});
	for(auto&& [entity, skinnedMeshRenderer, node] : skinnedMeshRenderers.each())
		if(_scene->getMeshes()[skinnedMeshRenderer.meshIndex].isValid())
			_instancesData.push_back({node.globalTransform});

	copyViaStagingBuffer(_instancesBuffer, _instancesData);
}

void Renderer::createVertexSkinningPipeline(VkPipelineCache pipelineCache) {
	if(_vertexSkinningPipeline)
		destroyVertexSkinningPipeline();

	_vertexSkinningDescriptorSetLayout = DescriptorSetLayoutBuilder()
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 0 Joints
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 1 Skin Joints
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 2 Skin Weights
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 3 Base Vertices
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 4 Output Vertices
											 .add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // 5 Output Motion Vectors
											 .build(*_device);
	VkPushConstantRange pushConstants{
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(VertexSkinningPushConstant),
	};
	_vertexSkinningPipeline.getLayout().create(*_device, {_vertexSkinningDescriptorSetLayout}, {pushConstants});
	Shader						vertexSkinningShader(*_device, "./shaders_spv/vertexSkinning.comp.spv");
	VkComputePipelineCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stage = vertexSkinningShader.getStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT),
		.layout = _vertexSkinningPipeline.getLayout(),
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = 0,
	};
	_vertexSkinningPipeline.create(*_device, info, pipelineCache);

	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	layoutsToAllocate.push_back(_vertexSkinningDescriptorSetLayout);
	_vertexSkinningDescriptorPool.create(*_device, layoutsToAllocate.size(),
										 std::array<VkDescriptorPoolSize, 1>{
											 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
										 });
	_vertexSkinningDescriptorPool.allocate(layoutsToAllocate);

	_currentJoints.init(*_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MaxJoints * sizeof(glm::mat4));

	// Copy base vertices. Skinning will only update the relevant data (position (& normal?))
	// FIXME: This is probably not the place to do this.
	_device->immediateSubmitTransfert([&](const CommandBuffer& commandBuffer) {
		auto					  instances = _scene->getRegistry().view<SkinnedMeshRendererComponent>();
		std::vector<VkBufferCopy> regions;
		for(auto& entity : instances) {
			auto& skinnedMeshRenderer = _scene->getRegistry().get<SkinnedMeshRendererComponent>(entity);
			auto  vertexSize = getMeshes()[skinnedMeshRenderer.meshIndex].getVertexByteSize();
			regions.push_back({
				.srcOffset = _offsetTable[getMeshes()[skinnedMeshRenderer.meshIndex].indexIntoOffsetTable].vertexOffset * sizeof(Vertex),
				.dstOffset = _skinnedOffsetTable[skinnedMeshRenderer.indexIntoOffsetTable - StaticOffsetTableSizeInBytes / sizeof(OffsetEntry)].vertexOffset * sizeof(Vertex),
				.size = vertexSize,
			});
		}
		if(!regions.empty())
			vkCmdCopyBuffer(commandBuffer, Vertices.buffer(), Vertices.buffer(), regions.size(), regions.data());
	});
}

void Renderer::writeSkinningDescriptorSet(const SkinnedMeshRendererComponent& comp) {
	const auto&			mesh = getMeshes()[comp.meshIndex];
	DescriptorSetWriter writer(_vertexSkinningDescriptorPool.getDescriptorSets()[0]);
	writer.add(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _currentJoints.buffer())
		.add(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mesh.getSkinJointsBuffer())
		.add(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mesh.getSkinWeightsBuffer())
		.add(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Vertices.buffer())
		.add(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Vertices.buffer())
		// We could bind the slice that we're interested in, but this would require us to adhere to storage alignement requirements,
		// I'll just pass the offsets as push constants, at least for now.
		.add(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MotionVectors.buffer())
		.update(*_device);
}

void Renderer::destroyVertexSkinningPipeline() {
	_vertexSkinningPipeline.destroy();
	_vertexSkinningDescriptorSetLayout.destroy();
	_vertexSkinningDescriptorPool.destroy();
	_currentJoints.free();
}
