#pragma once

#include <glm/glm.hpp>

#include <DescriptorPool.hpp>
#include <Pipeline.hpp>
#include <Query.hpp>
#include <RollingBuffer.hpp>
#include <Scene.hpp>

class StaticDeviceAllocator {
	void init(size_t capacity_) { capacity = capacity_; }

	DeviceMemory memory;
	Buffer		 buffer;
	size_t		 size = 0;
	size_t		 capacity = 0;
};

class Renderer {
  public:
	struct InstanceData {
		glm::mat4 transform{1.0f};
	};

	struct OffsetEntry {
		uint32_t materialIndex;
		uint32_t vertexOffset; // In number of vertices (not bytes)
		uint32_t indexOffset;  // In number of indices (not bytes)
	};

	enum InstanceMask : uint8_t {
		Static = 0x1,
		Dynamic = 0x2
	};

	Renderer() = default;
	Renderer(Scene& scene) : _scene(&scene) {}

	void setDevice(Device& device) { _device = &device; }
	void setScene(Scene& scene) { _scene = &scene; }

	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }
	inline const Buffer&					 getInstanceBuffer() const { return _instancesBuffer; }
	inline const auto&						 getDynamicOffsetTable() const { return _dynamicOffsetTable; }

	const RollingBuffer<float>& getDynamicBLASUpdateTimes() const { return _dynamicBLASUpdateTimes; }
	const RollingBuffer<float>& getTLASUpdateTimes() const { return _tlasUpdateTimes; }
	const RollingBuffer<float>& getCPUBLASUpdateTimes() const { return _cpuBLASUpdateTimes; }
	const RollingBuffer<float>& getCPUTLASUpdateTimes() const { return _cpuTLASUpdateTimes; }

	void createAccelerationStructure();
	void createTLAS();
	void destroyAccelerationStructure();
	void destroyTLAS();

	void updateTLAS();
	void updateTransforms();
	void updateAccelerationStructureInstances();

	// Allocate memory for all meshes in the scene
	void allocateMeshes();
	void updateMeshOffsetTable();
	void uploadMeshOffsetTable();

	void allocateDynamicMeshes();
	void updateDynamicMeshOffsetTable();
	void uploadDynamicMeshOffsetTable();

	void onHierarchicalChanges();
	void update();
	bool updateAnimations(float deltaTime);
	bool updateDynamicVertexBuffer();
	bool updateDynamicBLAS();

	void createVertexSkinningPipeline();
	void destroyVertexSkinningPipeline();

	void freeMeshesDeviceMemory();

	void free();

  private:
	Scene*	_scene = nullptr;
	Device* _device = nullptr;

	inline auto& getMeshes() const { return _scene->getMeshes(); }

	DeviceMemory			 OffsetTableMemory;
	DeviceMemory			 VertexMemory;
	DeviceMemory			 IndexMemory;
	DeviceMemory			 JointsMemory;
	DeviceMemory			 WeightsMemory;
	size_t					 NextVertexMemoryOffsetInBytes = 0;
	size_t					 NextIndexMemoryOffsetInBytes = 0;
	size_t					 NextJointsMemoryOffsetInBytes = 0;
	size_t					 NextWeightsMemoryOffsetInBytes = 0;
	Buffer					 VertexBuffer;
	Buffer					 IndexBuffer;
	Buffer					 OffsetTableBuffer;
	uint32_t				 StaticVertexBufferSizeInBytes = 0;
	uint32_t				 StaticIndexBufferSizeInBytes = 0;
	uint32_t				 StaticOffsetTableSizeInBytes = 0;
	uint32_t				 StaticJointsBufferSizeInBytes = 0;
	uint32_t				 StaticWeightsBufferSizeInBytes = 0;
	std::vector<OffsetEntry> _offsetTable;
	Buffer					 _blasScratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
	DeviceMemory			 _blasScratchMemory;
	// FIXME: This scratch buffer is used for static AND dynamic BLAS, all the static portion isn't used at all after the initial BLAS building, this should be better allocated
	// (the easiest is wimply to separate BLAS building into two pass, static and dynamic, sharing no memory).

	// Data for dynamic (skinned) meshes.
	const uint32_t											 MaxDynamicBLAS = 1024;
	const uint32_t											 MaxDynamicVertexSizeInBytes = 512 * 1024 * 1024;
	uint32_t												 DynamicOffsetTableSizeInBytes;
	std::vector<OffsetEntry>								 _dynamicOffsetTable;
	std::vector<VkAccelerationStructureGeometryKHR>			 _dynamicBLASGeometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> _dynamicBLASBuildGeometryInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 _dynamicBLASBuildRangeInfos;

	Buffer											_staticBLASBuffer;
	DeviceMemory									_staticBLASMemory;
	Buffer											_dynamicBLASBuffer;
	DeviceMemory									_dynamicBLASMemory;
	Buffer											_tlasBuffer;
	DeviceMemory									_tlasMemory;
	VkAccelerationStructureKHR						_topLevelAccelerationStructure;
	std::vector<VkAccelerationStructureKHR>			_bottomLevelAccelerationStructures;
	std::vector<VkAccelerationStructureKHR>			_dynamicBottomLevelAccelerationStructures;
	std::vector<VkAccelerationStructureInstanceKHR> _accStructInstances;
	Buffer											_accStructInstancesBuffer;
	DeviceMemory									_accStructInstancesMemory;

	std::vector<InstanceData> _instancesData; // Transforms for each instances
	Buffer					  _instancesBuffer;
	DeviceMemory			  _instancesMemory;

	// Reusable temp buffer(s)
	Buffer		 _tlasScratchBuffer;
	DeviceMemory _tlasScratchMemory;

	std::vector<QueryPool> _updateQueryPools;
	RollingBuffer<float>   _dynamicBLASUpdateTimes;
	RollingBuffer<float>   _tlasUpdateTimes;
	RollingBuffer<float>   _cpuTLASUpdateTimes;
	RollingBuffer<float>   _cpuBLASUpdateTimes;

	DescriptorPool		_vertexSkinningDescriptorPool;
	DescriptorSetLayout _vertexSkinningDescriptorSetLayout;
	Pipeline			_vertexSkinningPipeline;
	const uint32_t		MaxJoints = 512;
	Buffer				_jointsBuffer;
	DeviceMemory		_jointsMemory;
	void				writeSkinningDescriptorSet(const SkinnedMeshRendererComponent&);

	void sortRenderers();

	// FIXME: Should not be there.
	template<typename T>
	void copyViaStagingBuffer(Buffer& buffer, const std::vector<T>& data, uint32_t srcOffset = 0, uint32_t dstOffset = 0) {
		Buffer		 stagingBuffer;
		DeviceMemory stagingMemory;
		stagingBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, sizeof(T) * data.size());
		stagingMemory.allocate(device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		stagingMemory.fill(data.data(), data.size());

		_device->immediateSubmitTransfert([&](const CommandBuffer& cmdBuff) {
			VkBufferCopy copyRegion{
				.srcOffset = srcOffset,
				.dstOffset = dstOffset,
				.size = sizeof(T) * data.size(),
			};
			vkCmdCopyBuffer(cmdBuff, stagingBuffer, buffer, 1, &copyRegion);
		});
	}
};
