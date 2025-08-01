#pragma once

#include <glm/glm.hpp>

#include <DescriptorPool.hpp>
#include <IrradianceProbes.hpp>
#include <Pipeline.hpp>
#include <Query.hpp>
#include <RollingBuffer.hpp>
#include <Scene.hpp>
#include <StaticDeviceAllocator.hpp>
#include <vulkan/AccelerationStructure.hpp>

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
	inline const auto&						 getDynamicOffsetTable() const { return _skinnedOffsetTable; }

	const RollingBuffer<float>& getDynamicBLASUpdateTimes() const { return _skinnedBLASUpdateTimes; }
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

	void allocateSkinnedMeshes();
	void updateSkinnedMeshOffsetTable();
	void uploadSkinnedMeshOffsetTable();

	void onHierarchicalChanges(float deltaTime);
	void update();
	void createBLAS(MeshIndex idx); // Create and build the BLAS associated to supplied mesh idx
	void updateBLAS(MeshIndex idx);
	bool updateAnimations(float deltaTime); // FIXME: This should probably not be in the Renderer
	bool updateSkinnedVertexBuffer();
	bool updateSkinnedBLAS();

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
	size_t				  StaticVertexBufferSizeInBytes = 0;
	size_t				  StaticIndexBufferSizeInBytes = 0;
	size_t				  StaticOffsetTableSizeInBytes = 0;
	size_t				  StaticJointsBufferSizeInBytes = 0;
	size_t				  StaticWeightsBufferSizeInBytes = 0;

  private:
	Scene*	_scene = nullptr;
	Device* _device = nullptr;

	std::vector<OffsetEntry> _offsetTable;

	// Data for dynamic (skinned) meshes.
	const uint32_t											 MaxSkinnedBLAS = 1024;
	const size_t											 MaxSkinnedVertexSizeInBytes = 512 * 1024 * 1024;
	size_t													 SkinnedOffsetTableSizeInBytes = 0;
	std::vector<OffsetEntry>								 _skinnedOffsetTable;
	std::vector<VkAccelerationStructureGeometryKHR>			 _skinnedBLASGeometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> _skinnedBLASBuildGeometryInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 _skinnedBLASBuildRangeInfos;

	StaticDeviceAllocator							_blasMemory;
	Buffer											_tlasBuffer;
	DeviceMemory									_tlasMemory;
	VkAccelerationStructureKHR						_topLevelAccelerationStructure = VK_NULL_HANDLE;
	std::vector<AccelerationStructure>				_bottomLevelAccelerationStructures;
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
	// FIXME: This scratch buffer is used for static AND dynamic BLAS, all the static portion isn't used at all after the initial BLAS building, this should be better allocated
	// (the easiest is wimply to separate BLAS building into two pass, static and dynamic, sharing no memory).
	Buffer		 _blasScratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
	DeviceMemory _blasScratchMemory;

	std::vector<QueryPool> _updateQueryPools;
	RollingBuffer<float>   _skinnedBLASUpdateTimes;
	RollingBuffer<float>   _tlasUpdateTimes;
	RollingBuffer<float>   _cpuTLASUpdateTimes;
	RollingBuffer<float>   _cpuBLASUpdateTimes;

	DescriptorPool		  _vertexSkinningDescriptorPool;
	DescriptorSetLayout	  _vertexSkinningDescriptorSetLayout;
	Pipeline			  _vertexSkinningPipeline;
	const uint32_t		  MaxJoints = 1024;
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
