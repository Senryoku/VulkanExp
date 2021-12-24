#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT bool isBackface;

void main()
{
	isBackface = (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT);
}
