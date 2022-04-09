#version 460

#include "ProbeGrid.glsl"

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMetalness;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedoRoughness;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput inputEmissive;
layout (input_attachment_index = 4, set = 0, binding = 4) uniform subpassInput inputDirectLight;
#include "Lights.glsl"
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
	vec3 origin;
	uint frameIndex;
} ubo;
layout(set = 0, binding = 11) uniform LightUBO {
	Light DirectionalLight;
};

#include "irradiance.glsl"

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

#include "pbrMetallicRoughness.glsl"
#include "sky.glsl"

void main() {
	color = vec4(0.0, 0.0, 0.0, 1.0);
	
	vec3 origin = (inverse(ubo.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec4 positionDepth = subpassLoad(inputPositionDepth);
	vec3 position = positionDepth.xyz;
	float depth = positionDepth.w;
	vec4 normalMetalness = subpassLoad(inputNormalMetalness);
	vec3 normal = normalize(normalMetalness.xyz);
	float metalness = normalMetalness.w;
	vec4 albedoRoughness = subpassLoad(inputAlbedoRoughness);
	vec4 albedo = vec4(albedoRoughness.rgb, 1.0);
	float roughness = albedoRoughness.a;
	vec4 emissive = subpassLoad(inputEmissive);
	vec4 reflection = texture(inputReflection, fragPosition); 

	// Miss: Display the environment map.
	if(depth <= 0) {
		color.rgb = sky(origin, (inverse(ubo.view) * vec4(normalize(vec3(inverse(ubo.proj) * vec4(2.0 * fragPosition - 1.0, 0.0, 1.0))), 0)).xyz, DirectionalLight.direction.xyz, DirectionalLight.color.rgb, 1.0, true);
	} else {
		vec3 view = normalize(origin - position);
		// Direct Light
		color.rgb += subpassLoad(inputDirectLight).r * pbrMetallicRoughness(normal, view, DirectionalLight.color.rgb, DirectionalLight.direction.xyz, albedo, metalness, roughness).rgb;
	
		vec3 f0 = vec3(0.004); // Note: 0.04 Produce too much reflections on non-metallic materials for my taste.
		vec3 diffuseColor = albedo.rgb * (1.0 - f0);
		diffuseColor *= (1.0 - metalness);

		// Specular. FIXME: Still have no idea if this makes any sense.
		vec3 specularColor = mix(f0, albedo.rgb, metalness);
		color.rgb += specularColor * reflection.rgb;
	
		// Indirect Light (Radiance from probes)
		vec3 indirectLight = sampleProbes(position, normal, view, grid, irradianceColor, irradianceDepth).rgb;
		color.rgb += indirectLight * diffuseColor;

		// Emissive
		color.rgb += emissive.rgb;
	}
}
