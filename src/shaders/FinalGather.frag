#version 460

#include "irradiance.glsl"

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMaterial;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedo;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput inputReflection;
layout(binding = 5, set = 0) buffer MaterialsBlock { uint Materials[]; };
layout(binding = 6, set = 0) uniform UBOBlock {
	ProbeGrid grid;
};
layout(binding = 7, set = 0) uniform sampler2D irradianceColor;
layout(binding = 8, set = 0) uniform sampler2D irradianceDepth;
layout(binding = 9, set = 0) uniform UniformBufferObject 
{
    mat4 view;
    mat4 proj;
} ubo;

#include "Material.glsl"

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

#include "pbrMetallicRoughness.glsl"

vec3 LightDirection = vec3(1.0, -6.0, 2.0);

void main() {
	color = vec4(0.0, 0.0, 0.0, 1.0);
	vec3 origin = (ubo.view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec4 positionDepth = subpassLoad(inputPositionDepth);
	vec3 position = positionDepth.xyz;
	vec4 normalMaterial = subpassLoad(inputNormalMaterial);
	vec3 normal = normalMaterial.xyz;
	uint materialIndex = floatBitsToUint(normalMaterial.z);
	Material material = unpackMaterial(Materials[materialIndex]);
	vec4 albedo = subpassLoad(inputAlbedo);
	vec4 reflection = subpassLoad(inputReflection); // TODO: Select LOD from roughness + depth
	vec3 specularLight = reflection.rgb;
	vec3 lightColor = vec3(1.0); // TODO: Load from direct lighting
	vec3 indirectLight = vec3(0.0); // TODO: Load from probes
	color.rgb += pbrMetallicRoughness(normal, normalize(position - origin), lightColor, LightDirection, specularLight, albedo, material.metallicFactor, material.roughnessFactor).rgb;

}