#version 460

#include "irradiance.glsl"

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMaterial;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedo;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput inputReflection;
layout (input_attachment_index = 4, set = 0, binding = 4) uniform subpassInput inputDirectLight;
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

	// Direct Light
	color.rgb += subpassLoad(inputDirectLight).rgb;

	// Specular (???)
	vec3 specularLight = reflection.rgb;
	vec3 view = normalize(position - origin);
	vec3 halfVector = normal; // normalize(light + (-view)), but here light = reflect(-view, normal)
	float VdotH = clamp(dot(view, halfVector), 0.0, 1.0);
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = albedo.rgb * (1.0 - f0);
	diffuseColor *= (1.0 - material.metallicFactor);
	vec3 specularColor = mix(f0, diffuseColor, material.metallicFactor);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	vec3 specularEnvironmentR0 = specularColor.rgb;	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
	vec3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
	color.rgb += (F /*Missing BRDF parameters */) * pbrMetallicRoughness(normal, view, reflection.rgb, reflect(-view, normal), albedo, material.metallicFactor, material.roughnessFactor).rgb;
	
	// Indirect Light (Radiance from probes)
	vec3 indirectLight = sampleProbes(position, normal, grid, irradianceColor, irradianceDepth).rgb;  
	color.rgb += indirectLight * albedo.rgb;
}