#include "Mesh.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <JSON.hpp>
#include <Logger.hpp>
#include <stringutils.hpp>

void SubMesh::normalizeVertices() {
	glm::vec3 acc{0};
	for(const auto& v : _vertices)
		acc += v.pos;
	acc /= _vertices.size();
	for(auto& v : _vertices)
		v.pos -= acc;
}

void SubMesh::computeVertexNormals() {
	// Here, normals are the average of adjacent triangles' normals
	// (so we have exactly one normal per vertex)
	for(auto& v : _vertices) {
		v.normal = glm::vec3{0.0f};
		v.tangent = glm::vec4{0.0f};
	}

	for(size_t i = 0; i < _indices.size();) {
		// Norm of this triangle
		glm::vec3 norm = glm::normalize(glm::cross(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos, _vertices[_indices[i + 2]].pos - _vertices[_indices[i]].pos));
		glm::vec4 tang = glm::vec4{glm::normalize(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos), 1.0};
		// Add it to each vertex
		for(size_t j = 0; j < 3; ++j) {
			_vertices[_indices[i]].normal += norm;
			_vertices[_indices[i]].tangent += tang;
			++i;
		}
	}
	// Average norms
	for(auto& v : _vertices) {
		v.normal = v.normal / v.tangent.w;
		// FIXME: Use UVs when available to properly compute tangents
		v.tangent = glm::vec4{glm::vec3{v.tangent} / v.tangent.w, 1.0f};
	}
}

void SubMesh::computeBounds() {
	Bounds b{.min = _vertices[0].pos, .max = _vertices[0].pos};
	for(const auto& v : _vertices) {
		b.min = glm::min(b.min, v.pos);
		b.max = glm::max(b.max, v.pos);
	}
	_bounds = b;
}
