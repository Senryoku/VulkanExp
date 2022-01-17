#pragma once

#include <filesystem>

#include "vulkan/Mesh.hpp"

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

	class Attributes {};

	struct SubScene {
		std::string			  name;
		std::vector<uint32_t> nodes; // Indices in _nodes
	};

	using NodeIndex = uint32_t;
	static const NodeIndex InvalidNodeIndex = -1;
	using MeshIndex = uint32_t;
	static const MeshIndex InvalidMeshIndex = -1;

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

	struct Primitive {
		RenderingMode mode;
		Attributes	  attributes;
		size_t		  material;
	};

	// FIXME: Loading multiple glTF successively is completly broken
	enum class LoadOperation
	{
		AllScenes,
		AppendToCurrentScene
	};

	Scene() = default;
	Scene(std::filesystem::path path, LoadOperation loadOp = LoadOperation::AllScenes);
	~Scene();

	void loadglTF(std::filesystem::path path, LoadOperation loadOp = LoadOperation::AllScenes);

	void createAccelerationStructure(const Device& device);

	inline std::vector<Mesh>&				 getMeshes() { return _meshes; }
	inline std::vector<SubScene>&			 getScenes() { return _scenes; }
	inline std::vector<Node>&				 getNodes() { return _nodes; }
	inline const std::vector<Mesh>&			 getMeshes() const { return _meshes; }
	inline const std::vector<SubScene>&		 getScenes() const { return _scenes; }
	inline const std::vector<Node>&			 getNodes() const { return _nodes; }
	inline const VkAccelerationStructureKHR& getTLAS() const { return _topLevelAccelerationStructure; }

	inline void markDirty(Node* node) { _dirtyNodes.push_back(node); }
	bool		update();

	// Returns a dummy node with stands for the current scene (since it can have multiple children).
	inline const Node& getRoot() const { return _root; }

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

	///////////////////////////////////////////////////////////////////////////////////////
	// TODO: Cleanup
	struct OffsetEntry {
		uint32_t materialIndex;
		uint32_t vertexOffset;
		uint32_t indexOffset;
	};

	DeviceMemory OffsetTableMemory;
	DeviceMemory VertexMemory;
	DeviceMemory IndexMemory;
	size_t		 NextVertexMemoryOffset = 0;
	size_t		 NextIndexMemoryOffset = 0;
	Buffer		 OffsetTableBuffer;
	Buffer		 VertexBuffer;
	Buffer		 IndexBuffer;
	uint32_t	 OffsetTableSize;

	// Allocate memory for all meshes in the scene
	void allocateMeshes(const Device& device);
	void free(const Device& device);
	///////////////////////////////////////////////////////////////////////////////////////

  private:
	uint32_t			  _defaultScene = 0;
	Node				  _root;
	std::vector<Mesh>	  _meshes;
	std::vector<SubScene> _scenes;
	std::vector<Node>	  _nodes;

	std::vector<Node*> _dirtyNodes;

	Bounds _bounds;

	Buffer									_staticBLASBuffer;
	DeviceMemory							_staticBLASMemory;
	Buffer									_tlasBuffer;
	DeviceMemory							_tlasMemory;
	VkAccelerationStructureKHR				_topLevelAccelerationStructure;
	std::vector<VkAccelerationStructureKHR> _bottomLevelAccelerationStructures;
	Buffer									_accStructInstancesBuffer;
	DeviceMemory							_accStructInstancesMemory;
};
