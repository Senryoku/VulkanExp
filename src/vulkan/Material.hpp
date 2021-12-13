#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Resources.hpp"

#include <JSON.hpp>

class Material {
  public:
	std::string name;
	glm::vec4	baseColorFactor{1.0};
	float		metallicFactor = 1.0;
	float		roughnessFactor = 1.0;
	glm::vec3	emissiveFactor{0.0f};
	uint32_t	albedoTexture = -1;
	uint32_t	normalTexture = -1;
	uint32_t	metallicRoughnessTexture = -1;
	uint32_t	emissiveTexture = -1;

	// TODO: Move this (to a Scene class?)
	struct GPUData {
		float	  metallicFactor = 1.0;
		float	  roughnessFactor = 1.0;
		glm::vec3 emissiveFactor{0.0f};
		uint32_t  albedoTexture = -1;
		uint32_t  normalTexture = -1;
		uint32_t  metallicRoughnessTexture = -1;
		uint32_t  emissiveTexture = -1;
	};

	GPUData getGPUData() const { return GPUData{metallicFactor, roughnessFactor, emissiveFactor, albedoTexture, normalTexture, metallicRoughnessTexture, emissiveTexture}; }

  private:
};
