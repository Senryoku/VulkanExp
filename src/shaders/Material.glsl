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
};

// Expect an array named "Materials" to be accessible
Material unpackMaterial(uint index) {
	uint offset = index * 9;
	Material m;
	m.metallicFactor = uintBitsToFloat(Materials[offset + 0]);
	m.roughnessFactor = uintBitsToFloat(Materials[offset + 1]);
	m.emissiveFactor = vec3(uintBitsToFloat(Materials[offset + 2]), uintBitsToFloat(Materials[offset + 3]), uintBitsToFloat(Materials[offset + 4]));
	m.albedoTexture = Materials[offset + 5];
	m.normalTexture = Materials[offset + 6];
	m.metallicRoughnessTexture = Materials[offset + 7];
	m.emissiveTexture = Materials[offset + 8];
	return m;
}

#endif