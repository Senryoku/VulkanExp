#ifndef COMMON_GLSL
#define COMMON_GLSL

const float pi = 3.1415926538f;

vec3 rotateAxis(vec3 p, vec3 axis, float angle) {
	return mix(dot(axis, p) * axis, p, cos(angle)) + cross(axis, p) * sin(angle);
}

#endif