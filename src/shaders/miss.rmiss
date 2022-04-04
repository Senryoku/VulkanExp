#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_debug_printf : enable

#include "common.glsl"
#include "Lights.glsl"
layout(binding = 10, set = 0) uniform UBOLight {
	Light DirectionalLight;
};

#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;

#include "sky.glsl"

void main()
{
	payload.depth = -1.0f;
	payload.color.w = 0.0;
	// FIXME: The sun visible from this environnement map will also show up in reflections from glossy objects, but it's already handled by the direct light specular portion :(
	payload.color.rgb = sky(gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT, DirectionalLight.direction.xyz,  DirectionalLight.color.rgb, DirectionalLight.color.a, true);
}