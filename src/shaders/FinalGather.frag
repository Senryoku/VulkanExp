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
	vec4 albedoReflection = subpassLoad(inputAlbedoRoughness);
	vec4 albedo = vec4(albedoReflection.rgb, 1.0);
	float roughness = albedoReflection.a;
	// FIXME: The 4.0 factor is 100% arbitrary
	vec4 reflection = textureLod(inputReflection, fragPosition, roughness * 4.0); 

	// Direct Light
	color.rgb += subpassLoad(inputDirectLight).rgb;

	// Specular (???)
	vec3 view = normalize(position - origin);
	// FIXME: The (1.0 - roughness) is completely arbitrary. Figure out the proper attenuation once we also have a proper filtering.
	color.rgb += (1.0 - roughness) * albedo.rgb * reflection.rgb;
	
	// Indirect Light (Radiance from probes)
	vec3 indirectLight = sampleProbes(position, normal, -view, grid, irradianceColor, irradianceDepth).rgb;  
	color.rgb += indirectLight * albedo.rgb;
}