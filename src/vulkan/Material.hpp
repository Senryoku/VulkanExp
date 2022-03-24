#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Resources.hpp"

#include <JSON.hpp>
#include <TaggedType.hpp>

struct Material {
	struct Properties {
		float		 metallicFactor = 1.0;
		float		 roughnessFactor = 1.0;
		glm::vec3	 baseColorFactor{1.0f};
		glm::vec3	 emissiveFactor{0.0f};
		uint32_t	 albedoTexture = InvalidTextureIndex;
		uint32_t	 normalTexture = InvalidTextureIndex;
		uint32_t	 metallicRoughnessTexture = InvalidTextureIndex;
		uint32_t	 emissiveTexture = InvalidTextureIndex;
	};

	std::string name;
	Properties	properties;
};

struct MaterialIndexTag {};
using MaterialIndex = TaggedIndex<uint32_t, MaterialIndexTag>;
inline static const MaterialIndex InvalidMaterialIndex{static_cast<uint32_t>(-1)};

Material	parseMaterial(const JSON::value& obj, uint32_t textureOffset);
JSON::value toJSON(const Material& mat);
