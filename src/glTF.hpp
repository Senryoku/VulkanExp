#pragma once

#include <filesystem>

class glTF {
  public:
	glTF(std::filesystem::path path);
	~glTF();

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
};
