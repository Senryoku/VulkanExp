#version 460

#include "ProbeGrid.glsl"

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMaterial;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedo;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput inputReflection;
layout (input_attachment_index = 4, set = 0, binding = 4) uniform subpassInput inputDirectLight;
layout(binding = 5, set = 0) buffer MaterialsBlock { uint Materials[]; };
layout(binding = 6, set = 0) uniform UBOBlock {
	ProbeGrid grid;
};
layout(binding = 7, set = 0) buffer ProbesBlock { uint Probes[]; };
layout(binding = 8, set = 0) uniform sampler2D irradianceColor;
layout(binding = 9, set = 0) uniform sampler2D irradianceDepth;
layout(binding = 10, set = 0) uniform UniformBufferObject 
{
    mat4 view;
    mat4 proj;
} ubo;

#include "irradiance.glsl"
#include "Material.glsl"

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

#include "pbrMetallicRoughness.glsl"

void main() {
	color = vec4(0.0, 0.0, 0.0, 1.0);
	
	vec3 origin = (inverse(ubo.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec4 positionDepth = subpassLoad(inputPositionDepth);
	vec3 position = positionDepth.xyz;
	vec4 normalMaterial = subpassLoad(inputNormalMaterial);
	vec3 normal = normalMaterial.xyz;
	uint materialIndex = floatBitsToUint(normalMaterial.w);
	Material material = unpackMaterial(Materials[materialIndex]);
	vec4 albedo = subpassLoad(inputAlbedo);
	vec4 reflection = subpassLoad(inputReflection); // TODO: Select LOD from roughness + depth

	// Direct Light
	color.rgb += subpassLoad(inputDirectLight).rgb;

	// Specular (???)
	vec3 view = normalize(position - origin);
	//color.rgb += pbrMetallicRoughness(normal, -view, reflection.rgb, -reflect(-view, normal), albedo, material.metallicFactor, material.roughnessFactor).rgb;
	
	// Indirect Light (Radiance from probes)
	vec3 indirectLight = sampleProbes(position, normal, -view, grid, irradianceColor, irradianceDepth).rgb;  
	color.rgb += indirectLight * albedo.rgb;
}