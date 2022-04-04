#pragma once

#include <glm/glm.hpp>

#include <DescriptorPool.hpp>
#include <IrradianceProbes.hpp>
#include <Pipeline.hpp>
#include <Query.hpp>
#include <RollingBuffer.hpp>
#include <Scene.hpp>
#include <StaticDeviceAllocator.hpp>

class Renderer {
  public:
	struct InstanceData {
		glm::mat4 transform{1.0f};
		// Should the MaterielIndex be there?
	};

	struct OffsetEntry {
		uint32_t materialIndex;
		uint32_t vertexOffset; // In number of vertices (not bytes)
		uint32_t indexOffset;  // In number of indices (not bytes)
	};

	enum InstanceMask : uint8_t {
		Static = 0x1,
		Dynamic = 0x2,
		Skinned = 0x4,
	};

	Renderer() = default;
	Renderer(Scene& scene) : _scene(&scene) {}

	inline void setDevice(Device& device) { _device = &device; }
	inline void setScene(Scene& scene) { _scene = &scene; }

	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }
	inline const Buffer&					 getInstanceBuffer() const { return _instancesBuffer; }
	inline const Buffer&					 getPreviousInstanceBuffer() const { return _previousInstancesBuffer; }
	inline const auto&						 getDynamicOffsetTable() const { return _dynamicOffsetTable; }

	const RollingBuffer<float>& getDynamicBLASUpdateTimes() const { return _dynamicBLASUpdateTimes; }
	const RollingBuffer<float>& getTLASUpdateTimes() const { return _tlasUpdateTimes; }
	const RollingBuffer<float>& getCPUBLASUpdateTimes() const { return _cpuBLASUpdateTimes; }
	const RollingBuffer<float>& getCPUTLASUpdateTimes() const { return _cpuTLASUpdateTimes; }

	void createAccelerationStructures();
	void destroyAccelerationStructures();
	void createTLAS();
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

	void onHierarchicalChanges(float deltaTime);
	void update();
	bool updateAnimations(float deltaTime); // FIXME: This should probably not be in the Renderer
	bool updateDynamicVertexBuffer();
	bool updateDynamicBLAS();

	void createVertexSkinningPipeline(VkPipelineCache pipelineCache = VK_NULL_HANDLE);
	void destroyVertexSkinningPipeline();

	void freeMeshesDeviceMemory();

	void free();

	StaticDeviceAllocator OffsetTable;
	StaticDeviceAllocator Vertices;
	StaticDeviceAllocator Indices;
	StaticDeviceAllocator Joints;
	StaticDeviceAllocator Weights;
	StaticDeviceAllocator MotionVectors;
	uint32_t			  StaticVertexBufferSizeInBytes = 0;
	uint32_t			  StaticIndexBufferSizeInBytes = 0;
	uint32_t			  StaticOffsetTableSizeInBytes = 0;
	uint32_t			  StaticJointsBufferSizeInBytes = 0;
	uint32_t			  StaticWeightsBufferSizeInBytes = 0;

  private:
	Scene*	_scene = nullptr;
	Device* _device = nullptr;

	std::vector<OffsetEntry> _offsetTable;
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

	Buffer											_blasBuffer;
	DeviceMemory									_blasMemory;
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
	Buffer					  _previousInstancesBuffer;
	StaticDeviceAllocator	  _instancesMemory;

	// Reusable temp buffer(s)
	Buffer		 _tlasScratchBuffer;
	DeviceMemory _tlasScratchMemory;
	Buffer		 _blasScratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
	DeviceMemory _blasScratchMemory;

	std::vector<QueryPool> _updateQueryPools;
	RollingBuffer<float>   _dynamicBLASUpdateTimes;
	RollingBuffer<float>   _tlasUpdateTimes;
	RollingBuffer<float>   _cpuTLASUpdateTimes;
	RollingBuffer<float>   _cpuBLASUpdateTimes;

	DescriptorPool		  _vertexSkinningDescriptorPool;
	DescriptorSetLayout	  _vertexSkinningDescriptorSetLayout;
	Pipeline			  _vertexSkinningPipeline;
	const uint32_t		  MaxJoints = 512;
	StaticDeviceAllocator _currentJoints;

	void		 writeSkinningDescriptorSet(const SkinnedMeshRendererComponent&);
	void		 sortRenderers();
	inline auto& getMeshes() const { return _scene->getMeshes(); }

	StaticDeviceAllocator stagingBuffer;
	// FIXME: Should not be there.
	template<typename T>
	void copyViaStagingBuffer(const Buffer& buffer, const std::vector<T>& data, uint32_t srcOffset = 0, uint32_t dstOffset = 0) {
		auto byteSize = sizeof(T) * data.size();
		if(!stagingBuffer || stagingBuffer.capacity() < byteSize) {
			if(stagingBuffer)
				stagingBuffer.free();
			stagingBuffer.init(*_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, byteSize);
		}
		stagingBuffer.memory().fill(data.data(), data.size());

		_device->immediateSubmitTransfert([&](const CommandBuffer& cmdBuff) {
			VkBufferCopy copyRegion{
				.srcOffset = srcOffset,
				.dstOffset = dstOffset,
				.size = byteSize,
			};
			vkCmdCopyBuffer(cmdBuff, stagingBuffer.buffer(), buffer, 1, &copyRegion);
		});
	}
};
