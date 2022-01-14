#version 460

#include "ProbeGrid.glsl"

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMetalness;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedoRoughness;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput inputDirectLight;
layout (binding = 5, set = 0) uniform sampler2D inputReflection;
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

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

#include "pbrMetallicRoughness.glsl"

void main() {
	color = vec4(0.0, 0.0, 0.0, 1.0);
	
	vec3 origin = (inverse(ubo.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec4 positionDepth = subpassLoad(inputPositionDepth);
	vec3 position = positionDepth.xyz;
	vec4 normalMetalness = subpassLoad(inputNormalMetalness);
	vec3 normal = normalize(normalMetalness.xyz);
	float metalness = normalMetalness.w;
	vec4 albedoRoughness = subpassLoad(inputAlbedoRoughness);
	vec4 albedo = vec4(albedoRoughness.rgb, 1.0);
	float roughness = albedoRoughness.a;
	vec4 reflection = texture(inputReflection, fragPosition); 

	// Direct Light
	color.rgb += subpassLoad(inputDirectLight).rgb;
	
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = albedo.rgb * (1.0 - f0);
	diffuseColor *= (1.0 - metalness);

	// Specular (???)
	vec3 specularColor = mix(f0, albedo.rgb, metalness);
	color.rgb += specularColor * reflection.rgb;
	
	// Indirect Light (Radiance from probes)
	vec3 view = normalize(position - origin);
	vec3 indirectLight = sampleProbes(position, normal, -view, grid, irradianceColor, irradianceDepth).rgb;
	color.rgb += indirectLight * diffuseColor;
}