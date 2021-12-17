// FIXME: To Uniforms, and probably only the directional one.
struct Light {
	uint type; // 0 Directional, 1 Point light
	vec3 color;
	vec3 direction;
};

Light Lights[3] = {
	Light(0, vec3(5.0), normalize(vec3(-2, 1, -2))),
	Light(1, vec3(300.0, 100.0, 100.0), vec3(-620, 160, 143.5)),
	Light(1, vec3(300.0, 100.0, 100.0), vec3(487, 160, 143.5))
};