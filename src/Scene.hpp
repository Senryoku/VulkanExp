#pragma once

#include <filesystem>

#include "vulkan/Mesh.hpp"
#include <Raytracing.hpp>
#include <TaggedType.hpp>

// TODO: Move this :)
inline std::vector<Material> Materials;

class Scene {
  public:
	enum class RenderingMode
	{
		Points = 0,
		Line = 1,
		LineLoop = 2,
		LineStrip = 3,
		Triangles = 4,
		TriangleStrip = 5,
		TriangleFan = 6
	};

	enum class ComponentType
	{
		Byte = 5120,
		UnsignedByte = 5121,
		Short = 5122,
		UnsignedShort = 5123,
		Int = 5124,
		UnsignedInt = 5125,
		Float = 5126,
		Double = 5130,
	};

	struct NodeIndexTag {};
	struct MeshIndexTag {};

	using NodeIndex = TaggedIndex<uint32_t, NodeIndexTag>;
	inline static const NodeIndex InvalidNodeIndex{static_cast<uint32_t>(-1)};
	using MeshIndex = TaggedIndex<uint32_t, MeshIndexTag>;
	inline static const MeshIndex InvalidMeshIndex{static_cast<uint32_t>(-1)};

	struct Node {
		std::string			   name = "Unamed Node";
		glm::mat4			   transform = glm::mat4(1.0); // Relative to parent
		NodeIndex			   parent = InvalidNodeIndex;
		std::vector<NodeIndex> children;				// Indices in _nodes
		MeshIndex			   mesh = InvalidMeshIndex; // Index in global mesh array
	};

	void addChild(NodeIndex parent, NodeIndex child) {
		assert(_nodes[child].parent == InvalidNodeIndex);
		_nodes[parent].children.push_back(child);
		_nodes[child].parent = parent;
	}

	Scene();
	Scene(const std::filesystem::path& path);
	~Scene();

	bool load(const std::filesystem::path& path);
	bool loadglTF(const std::filesystem::path& path);
	bool loadOBJ(const std::filesystem::path& path);
	bool loadMaterial(const std::filesystem::path& path);

	bool save(const std::filesystem::path& path);

	void createAccelerationStructure(const Device& device);
	void destroyAccelerationStructure(const Device& device);

	inline std::vector<Mesh>&				 getMeshes() { return _meshes; }
	inline std::vector<Node>&				 getNodes() { return _nodes; }
	inline const std::vector<Mesh>&			 getMeshes() const { return _meshes; }
	inline const std::vector<Node>&			 getNodes() const { return _nodes; }
	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }

	inline void markDirty(NodeIndex node) { _dirtyNodes.push_back(node); }
	bool		update(const Device& device);
	void		updateTLAS(const Device& device);

	// Returns a dummy node with stands for the current scene (since it can have multiple children).
	inline const Node& getRoot() const { return _nodes[0]; }
	inline Node&	   getRoot() { return _nodes[0]; }

	NodeIndex intersectNodes(Ray& ray);

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	const Bounds&		 computeBounds() {
		   bool init = false;

		   std::function<void(const Node&, glm::mat4)> visitNode = [&](const Node& n, glm::mat4 transform) {
			   transform = transform * n.transform;
			   for(const auto& c : n.children)
				   visitNode(_nodes[c], transform);
			   if(n.mesh != InvalidMeshIndex)
				   if(!init) {
					   _bounds = transform * _meshes[n.mesh].computeBounds();
					   init = true;
				   } else
					   _bounds += transform * _meshes[n.mesh].computeBounds();
		   };
		   visitNode(getRoot(), glm::mat4(1.0f));

		   return _bounds;
	}

	Node& operator[](NodeIndex index) {
		assert(index != InvalidNodeIndex);
		return _nodes[index];
	}

	const Node& operator[](NodeIndex index) const {
		assert(index != InvalidNodeIndex);
		return _nodes[index];
	}

	Mesh& operator[](MeshIndex index) {
		assert(index != InvalidMeshIndex);
		return _meshes[index];
	}
	const Mesh& operator[](MeshIndex index) const {
		assert(index != InvalidNodeIndex);
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

  private:
	std::vector<std::filesystem::path> _loadedFiles; // Path of all currently loaded files.

	std::vector<Mesh> _meshes;
	std::vector<Node> _nodes;

	std::vector<NodeIndex> _dirtyNodes;

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

	// Reusable temp buffer(s)
	Buffer		 _tlasScratchBuffer;
	DeviceMemory _tlasScratchMemory;

	bool loadMaterial(const JSON::value& mat, uint32_t textureOffset);
	bool loadTextures(const std::filesystem::path& path, const JSON::value& json);
};
