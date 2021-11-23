#pragma once

#include <filesystem>

#include "vulkan/Mesh.hpp"

class glTF {
  public:
	glTF() = default;
	glTF(std::filesystem::path path);
	~glTF();

	void load(std::filesystem::path path);

	std::vector<Mesh>&		 getMeshes() { return _meshes; }
	const std::vector<Mesh>& getMeshes() const { return _meshes; }

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
	class Material {};

	class Scene {};
	class Node {};
	struct Primitive {
		RenderingMode mode;
		Attributes	  attributes;
		size_t		  material;
	};

  private:
	std::vector<Mesh> _meshes;
};
