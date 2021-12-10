#version 460
#extension GL_EXT_ray_tracing : enable

struct rayPayload {
	vec3 raydx;
	vec3 raydy;

	vec3 color; // Result
};

layout(location = 0) rayPayloadInEXT rayPayload payload;

void main()
{
    payload.color = vec3(0.5294117647, 0.80784313725, 0.92156862745);
}