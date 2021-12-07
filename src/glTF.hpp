#pragma once

#include <filesystem>

#include "vulkan/Mesh.hpp"

// TODO: Move this :)
inline std::vector<Material> Materials;

class glTF {
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

	struct Scene {
		std::string			  name;
		std::vector<uint32_t> nodes; // Indices in _nodes
	};

	struct Node {
		std::string			  name;
		glm::mat4			  transform; // Relative to parent
		std::vector<uint32_t> children;	 // Indices in _nodes
		uint32_t			  mesh = -1; // Index in global mesh array
	};

	struct Primitive {
		RenderingMode mode;
		Attributes	  attributes;
		size_t		  material;
	};

	glTF() = default;
	glTF(std::filesystem::path path);
	~glTF();

	void load(std::filesystem::path path);

	inline std::vector<Mesh>&		 getMeshes() { return _meshes; }
	inline std::vector<Scene>&		 getScenes() { return _scenes; }
	inline std::vector<Node>&		 getNodes() { return _nodes; }
	inline const std::vector<Mesh>&	 getMeshes() const { return _meshes; }
	inline const std::vector<Scene>& getScenes() const { return _scenes; }
	inline const std::vector<Node>&	 getNodes() const { return _nodes; }

	inline const Node& getRoot() const {
		assert(_scenes[_defaultScene].nodes.size() == 1); // I've yet to see a scene with multiple roots, maybe we'll have to fix this later
		return _nodes[_scenes[_defaultScene].nodes[0]];
	}

  private:
	uint32_t		   _defaultScene = 0;
	std::vector<Mesh>  _meshes;
	std::vector<Scene> _scenes;
	std::vector<Node>  _nodes;
};
