#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

struct Material {
	float metallicFactor;
	float roughnessFactor;
	vec3  emissiveFactor;
	uint  albedoTexture;
	uint  normalTexture;
	uint  metallicRoughnessTexture;
	uint  emissiveTexture;
	vec3  baseColorFactor;
};

#endif