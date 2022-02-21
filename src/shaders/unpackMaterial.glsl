#ifndef UNPACK_MATERIAL_GLSL
#define UNPACK_MATERIAL_GLSL

#include "Material.glsl"

// Expects an array named "Materials" to be accessible
Material unpackMaterial(uint index) {
	uint offset = index * 12;
	Material m;
	m.metallicFactor = uintBitsToFloat(Materials[offset + 0]);
	m.roughnessFactor = uintBitsToFloat(Materials[offset + 1]);
	m.emissiveFactor = vec3(uintBitsToFloat(Materials[offset + 2]), uintBitsToFloat(Materials[offset + 3]), uintBitsToFloat(Materials[offset + 4]));
	m.albedoTexture = Materials[offset + 5];
	m.normalTexture = Materials[offset + 6];
	m.metallicRoughnessTexture = Materials[offset + 7];
	m.emissiveTexture = Materials[offset + 8];
	m.baseColorFactor = vec3(uintBitsToFloat(Materials[offset + 9]), uintBitsToFloat(Materials[offset + 10]), uintBitsToFloat(Materials[offset + 11]));
	return m;
}

#endif