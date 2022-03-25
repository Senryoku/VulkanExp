#pragma once

#include <filesystem>

#include <Mesh.hpp>
#include <Query.hpp>
#include <Raytracing.hpp>
#include <RollingBuffer.hpp>
#include <TaggedType.hpp>

#include <entt.hpp>

// TODO: Move this :)
inline std::vector<Material> Materials;

struct NodeComponent {
	std::string	 name{"Unamed Node"};
	glm::mat4	 transform{1.0f};
	std::size_t	 children{0};
	entt::entity first{entt::null};
	entt::entity prev{entt::null};
	entt::entity next{entt::null};
	entt::entity parent{entt::null};
};

struct MeshIndexTag {};
using MeshIndex = TaggedIndex<uint32_t, MeshIndexTag>;
inline static const MeshIndex InvalidMeshIndex{static_cast<uint32_t>(-1)};
struct SkinIndexTag {};
using SkinIndex = TaggedIndex<uint32_t, SkinIndexTag>;
inline static const SkinIndex InvalidSkinIndex{static_cast<uint32_t>(-1)};

struct MeshRendererComponent {
	MeshIndex	  meshIndex = InvalidMeshIndex; // FIXME: Use something else.
	MaterialIndex materialIndex = InvalidMaterialIndex;
};

struct SkinnedMeshRendererComponent {
	MeshIndex	  meshIndex = InvalidMeshIndex; // FIXME: Use something else.
	MaterialIndex materialIndex = InvalidMaterialIndex;
	SkinIndex	  skinIndex = InvalidSkinIndex;
	size_t		  blasIndex = static_cast<size_t>(-1);
	uint32_t	  indexIntoOffsetTable = 0;
};

struct AnimationComponent {
	float		   time = 0;
	AnimationIndex animationIndex = InvalidAnimationIndex;
};

struct Skin {
	std::vector<glm::mat4>	  inverseBindMatrices;
	std::vector<entt::entity> joints;
};

class Scene {
  public:
	enum class RenderingMode {
		Points = 0,
		Line = 1,
		LineLoop = 2,
		LineStrip = 3,
		Triangles = 4,
		TriangleStrip = 5,
		TriangleFan = 6
	};

	enum class ComponentType {
		Byte = 5120,
		UnsignedByte = 5121,
		Short = 5122,
		UnsignedShort = 5123,
		Int = 5124,
		UnsignedInt = 5125,
		Float = 5126,
		Double = 5130,
	};

	struct InstanceData {
		glm::mat4 transform{1.0f};
	};

	void removeFromHierarchy(entt::entity);
	void addChild(entt::entity parent, entt::entity child);
	void addSibling(entt::entity target, entt::entity other);

	Scene();
	Scene(const std::filesystem::path& path);
	~Scene();

	bool load(const std::filesystem::path& path);
	bool loadglTF(const std::filesystem::path& path);
	bool loadOBJ(const std::filesystem::path& path);
	bool loadMaterial(const std::filesystem::path& path);
	bool loadScene(const std::filesystem::path& path);

	bool save(const std::filesystem::path& path);

	void						createAccelerationStructure(const Device& device);
	void						createTLAS(const Device& device);
	void						destroyAccelerationStructure(const Device& device);
	void						destroyTLAS(const Device& device);
	const RollingBuffer<float>& getDynamicBLASUpdateTimes() const { return _dynamicBLASUpdateTimes; }
	const RollingBuffer<float>& getTLASUpdateTimes() const { return _tlasUpdateTimes; }
	const RollingBuffer<float>& getCPUBLASUpdateTimes() const { return _cpuBLASUpdateTimes; }
	const RollingBuffer<float>& getCPUTLASUpdateTimes() const { return _cpuTLASUpdateTimes; }
	const RollingBuffer<float>& getUpdateTimes() const { return _updateTimes; }

	inline std::vector<Mesh>&				 getMeshes() { return _meshes; }
	inline const std::vector<Mesh>&			 getMeshes() const { return _meshes; }
	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }
	inline const Buffer&					 getInstanceBuffer() const { return _instancesBuffer; }
	inline std::vector<Skin>&				 getSkins() { return _skins; }
	inline const std::vector<Skin>&			 getSkins() const { return _skins; }

	inline void markDirty(entt::entity node) { _dirtyNodes.push_back(node); }
	bool		update(const Device& device, float deltaTime);
	void		updateTLAS(const Device& device);
	void		updateTransforms(const Device& device);
	void		updateAccelerationStructureInstances(const Device& device);

	inline entt::entity getRoot() const { return _root; }

	bool	  isAncestor(entt::entity ancestor, entt::entity entity) const;
	glm::mat4 getGlobalTransform(const NodeComponent& node) const;

	entt::entity intersectNodes(Ray& ray);

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	const Bounds&		 computeBounds() {
		   bool init = false;

		   forEachNode([&](entt::entity entity, glm::mat4 transform) {
			   if(auto* mesh = _registry.try_get<MeshRendererComponent>(entity); mesh != nullptr) {
				   if(!init) {
					   _bounds = transform * _meshes[mesh->meshIndex].computeBounds();
					   init = true;
				   } else
					   _bounds += transform * _meshes[mesh->meshIndex].computeBounds();
			   }
			   if(auto* mesh = _registry.try_get<SkinnedMeshRendererComponent>(entity); mesh != nullptr) {
				   if(!init) {
					   _bounds = transform * _meshes[mesh->meshIndex].computeBounds();
					   init = true;
				   } else
					   _bounds += transform * _meshes[mesh->meshIndex].computeBounds();
			   }
		   });

		   return _bounds;
	}

	// Depth-First traversal of the node hierarchy
	// Callback will be call for each entity with the entity and its world transformation as parameters.
	void forEachNode(const std::function<void(entt::entity entity, glm::mat4)>& call) { visitNode(getRoot(), glm::mat4(1.0f), call); }

	Mesh& operator[](MeshIndex index) {
		assert(index != InvalidMeshIndex);
		return _meshes[index];
	}
	const Mesh& operator[](MeshIndex index) const {
		assert(index != InvalidMeshIndex);
		return _meshes[index];
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// TODO: Cleanup
	struct OffsetEntry {
		uint32_t materialIndex;
		uint32_t vertexOffset; // In number of vertices (not bytes)
		uint32_t indexOffset;  // In number of indices (not bytes)
	};

	DeviceMemory			 OffsetTableMemory;
	DeviceMemory			 VertexMemory;
	DeviceMemory			 IndexMemory;
	size_t					 NextVertexMemoryOffsetInBytes = 0;
	size_t					 NextIndexMemoryOffsetInBytes = 0;
	Buffer					 VertexBuffer;
	Buffer					 IndexBuffer;
	Buffer					 OffsetTableBuffer;
	uint32_t				 StaticVertexBufferSizeInBytes;
	uint32_t				 StaticIndexBufferSizeInBytes;
	uint32_t				 StaticOffsetTableSizeInBytes;
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

	// Allocate memory for all meshes in the scene
	void allocateMeshes(const Device& device);
	void updateMeshOffsetTable();
	void uploadMeshOffsetTable(const Device& device);

	void allocateDynamicMeshes(const Device&);
	void updateDynamicMeshOffsetTable();
	void uploadDynamicMeshOffsetTable(const Device&);
	bool updateDynamicVertexBuffer(const Device& device, float deltaTime);
	bool updateDynamicBLAS(const Device&);

	void free(const Device& device);
	///////////////////////////////////////////////////////////////////////////////////////

	entt::registry&		  getRegistry() { return _registry; }
	const entt::registry& getRegistry() const { return _registry; }

  private:
	std::vector<Mesh> _meshes;
	std::vector<Skin> _skins;

	entt::registry			  _registry;
	entt::entity			  _root = entt::null;
	std::vector<entt::entity> _dirtyNodes; // FIXME: May not be useful anymore.

	Bounds _bounds;

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
	RollingBuffer<float>   _updateTimes;

	bool loadMaterial(const JSON::value& mat, uint32_t textureOffset);
	bool loadTextures(const std::filesystem::path& path, const JSON::value& json);

	// Called on NodeComponent destruction
	void onDestroyNodeComponent(entt::registry& registry, entt::entity node);

	// Used for depth-first traversal of the node hierarchy
	void visitNode(entt::entity entity, glm::mat4 transform, const std::function<void(entt::entity entity, glm::mat4)>& call) {
		const auto& node = _registry.get<NodeComponent>(entity);
		transform = transform * node.transform;
		for(auto c = node.first; c != entt::null; c = _registry.get<NodeComponent>(c).next)
			visitNode(c, transform, call);

		call(entity, transform);
	};

	void sortRenderers();

	// FIXME: Should not be there.
	template<typename T>
	void copyViaStagingBuffer(const Device& device, Buffer& buffer, const std::vector<T>& data, uint32_t srcOffset = 0, uint32_t dstOffset = 0) {
		Buffer		 stagingBuffer;
		DeviceMemory stagingMemory;
		stagingBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, sizeof(T) * data.size());
		stagingMemory.allocate(device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		stagingMemory.fill(data.data(), data.size());

		device.immediateSubmitTransfert([&](const CommandBuffer& cmdBuff) {
			VkBufferCopy copyRegion{
				.srcOffset = srcOffset,
				.dstOffset = dstOffset,
				.size = sizeof(T) * data.size(),
			};
			vkCmdCopyBuffer(cmdBuff, stagingBuffer, buffer, 1, &copyRegion);
		});
	}
};

JSON::value toJSON(const NodeComponent&);
