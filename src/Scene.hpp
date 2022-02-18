#pragma once

#include <filesystem>

#include "vulkan/Mesh.hpp"
#include <Raytracing.hpp>
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

struct MeshComponent {
	MeshIndex index; // FIXME: Use something else.
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

	void addChild(entt::entity parent, entt::entity child) {
		assert(parent != child);
		auto& parentNode = _registry.get<NodeComponent>(parent);
		auto& childNode = _registry.get<NodeComponent>(child);
		assert(childNode.parent == entt::null); // We should probably handle this case, but we don't right now!
		if(parentNode.first == entt::null) {
			parentNode.first = child;
		} else {
			auto lastChild = parentNode.first;
			while(_registry.get<NodeComponent>(lastChild).next != entt::null) {
				lastChild = _registry.get<NodeComponent>(lastChild).next;
			}
			_registry.get<NodeComponent>(lastChild).next = child;
			childNode.prev = lastChild;
		}
		childNode.parent = parent;
		++parentNode.children;
		markDirty(parent);
		markDirty(child);
	}
	void addSibling(entt::entity target, entt::entity other) {
		assert(target != other);
		auto& targetNode = _registry.get<NodeComponent>(target);
		auto& otherNode = _registry.get<NodeComponent>(other);
		assert(otherNode.parent == entt::null); // We should probably handle this case, but we don't right now!
		auto& parentNode = _registry.get<NodeComponent>(targetNode.parent);
		++parentNode.children;
		otherNode.parent = targetNode.parent;
		if(targetNode.next != entt::null)
			_registry.get<NodeComponent>(targetNode.next).prev = other;
		otherNode.next = targetNode.next;
		otherNode.prev = target;
		targetNode.next = other;
	}

	Scene();
	Scene(const std::filesystem::path& path);
	~Scene();

	bool load(const std::filesystem::path& path);
	bool loadglTF(const std::filesystem::path& path);
	bool loadOBJ(const std::filesystem::path& path);
	bool loadMaterial(const std::filesystem::path& path);
	bool loadScene(const std::filesystem::path& path);

	bool save(const std::filesystem::path& path);

	void createAccelerationStructure(const Device& device);
	void createTLAS(const Device& device);
	void destroyAccelerationStructure(const Device& device);
	void destroyTLAS(const Device& device);

	inline std::vector<Mesh>&				 getMeshes() { return _meshes; }
	inline const std::vector<Mesh>&			 getMeshes() const { return _meshes; }
	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }

	inline void markDirty(entt::entity node) { _dirtyNodes.push_back(node); }
	bool		update(const Device& device);
	void		updateTLAS(const Device& device);

	inline entt::entity getRoot() const { return _root; }

	entt::entity intersectNodes(Ray& ray);

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	const Bounds&		 computeBounds() {
		   bool init = false;

		   forEachNode([&](entt::entity entity, glm::mat4 transform) {
			   if(auto* mesh = _registry.try_get<MeshComponent>(entity); mesh != nullptr) {
				   if(!init) {
					   _bounds = transform * _meshes[mesh->index].computeBounds();
					   init = true;
				   } else
					   _bounds += transform * _meshes[mesh->index].computeBounds();
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
		uint32_t vertexOffset;
		uint32_t indexOffset;
	};

	DeviceMemory			 OffsetTableMemory;
	DeviceMemory			 VertexMemory;
	DeviceMemory			 IndexMemory;
	size_t					 NextVertexMemoryOffset = 0;
	size_t					 NextIndexMemoryOffset = 0;
	Buffer					 OffsetTableBuffer;
	Buffer					 VertexBuffer;
	Buffer					 IndexBuffer;
	uint32_t				 OffsetTableSize;
	std::vector<OffsetEntry> _offsetTable;

	// Allocate memory for all meshes in the scene
	void allocateMeshes(const Device& device);
	void free(const Device& device);
	void updateMeshOffsetTable();
	void uploadMeshOffsetTable(const Device& device);
	///////////////////////////////////////////////////////////////////////////////////////

	entt::registry&		  getRegistry() { return _registry; }
	const entt::registry& getRegistry() const { return _registry; }

  private:
	std::vector<Mesh> _meshes;

	entt::registry			  _registry;
	entt::entity			  _root = entt::null;
	std::vector<entt::entity> _dirtyNodes; // FIXME: May not be useful anymore.

	Bounds _bounds;

	Buffer											_staticBLASBuffer;
	DeviceMemory									_staticBLASMemory;
	Buffer											_tlasBuffer;
	DeviceMemory									_tlasMemory;
	VkAccelerationStructureKHR						_topLevelAccelerationStructure;
	std::vector<VkAccelerationStructureKHR>			_bottomLevelAccelerationStructures;
	std::vector<VkAccelerationStructureInstanceKHR> _accStructInstances;
	Buffer											_accStructInstancesBuffer;
	DeviceMemory									_accStructInstancesMemory;
	std::vector<std::vector<size_t>>				_submeshesIndicesIntoBLASArray;

	// Reusable temp buffer(s)
	Buffer		 _tlasScratchBuffer;
	DeviceMemory _tlasScratchMemory;

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
};

JSON::value toJSON(const NodeComponent&);
