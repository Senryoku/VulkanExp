#version 460
#extension GL_EXT_ray_tracing : require

struct payload_t {
	bool isBackface;
	float depth;
};

layout(location = 0) rayPayloadInEXT payload_t payload;

void main()
{
	payload.depth = gl_HitTEXT;
	payload.isBackface = (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT);
}
