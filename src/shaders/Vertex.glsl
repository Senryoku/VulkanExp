#ifndef VERTEX_GLSL
#define VERTEX_GLSL

struct Vertex {
	vec3 pos;
	vec3 color;
	vec3 normal;
	vec4 tangent;
	vec2 texCoord;
	uint material;
};

// Expect an array named "Vertices" to be accessible
Vertex unpack(uint index)
{
	// Unpack the vertices from the SSBO
	const int vertexSizeInBytes = 4 * 16;
	const int stride = vertexSizeInBytes / 16;

	vec4 d0 = Vertices[stride * index + 0];
	vec4 d1 = Vertices[stride * index + 1];
	vec4 d2 = Vertices[stride * index + 2];
	vec4 d3 = Vertices[stride * index + 3];

	Vertex v;
	v.pos = d0.xyz;
	v.color = vec3(d0.w, d1.x, d1.y);
	v.normal = vec3(d1.z, d1.w, d2.x);
	v.tangent = vec4(d2.y, d2.z, d2.w, d3.x);
	v.texCoord = vec2(d3.y, d3.z);
	v.material = floatBitsToUint(d3.w);

	return v;
}
#endif