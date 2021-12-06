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
		std::vector<uint32_t> nodes;
	};

	struct Node {
		std::string			  name;
		glm::mat4			  transform;
		std::vector<uint32_t> children;
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

	std::vector<Mesh>&		  getMeshes() { return _meshes; }
	const std::vector<Mesh>&  getMeshes() const { return _meshes; }
	std::vector<Scene>&		  getScenes() { return _scenes; }
	const std::vector<Scene>& getScenes() const { return _scenes; }
	std::vector<Node>&		  getNodes() { return _nodes; }
	const std::vector<Node>&  getNodes() const { return _nodes; }

  private:
	std::vector<Mesh>  _meshes;
	std::vector<Scene> _scenes;
	std::vector<Node>  _nodes;
};
