#version 460
#extension GL_EXT_ray_tracing : enable

#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;

void main()
{
	payload.color = vec4(0.0);
	payload.depth = -1.0f;
    //payload.color = vec3(0.5294117647, 0.80784313725, 0.92156862745);
}