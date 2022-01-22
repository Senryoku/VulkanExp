#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Resources.hpp"

#include <JSON.hpp>

struct Material {
	using TextureIndex = uint32_t;
	static const TextureIndex InvalidTextureIndex = -1;

	struct Properties {
		// glm::vec4 baseColorFactor{1.0};
		float	  metallicFactor = 1.0;
		float	  roughnessFactor = 1.0;
		glm::vec3 emissiveFactor{0.0f};
		TextureIndex albedoTexture = InvalidTextureIndex;
		TextureIndex normalTexture = InvalidTextureIndex;
		TextureIndex metallicRoughnessTexture = InvalidTextureIndex;
		TextureIndex emissiveTexture = InvalidTextureIndex;
	};

	std::string name;
	Properties	properties;
};
