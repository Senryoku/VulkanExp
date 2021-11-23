#include "Mesh.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <JSON.hpp>
#include <Logger.hpp>
#include <stringutils.hpp>

bool Mesh::loadOBJ(const std::filesystem::path& path) {
	std::ifstream file{path};

	_vertices.clear();
	_indices.clear();
	_vertices.reserve(65536);
	_indices.reserve(4 * 65536);

	std::string line;
	Vertex		v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
	while(std::getline(file, line)) {
		// Ignore empty lines and comments
		if(line.empty() || line[line.find_first_not_of(" \t")] == '#')
			continue;
		// Remove comments
		auto size = line.find('#');
		if(size == line.npos)
			size = line.size();
		std::string_view line_view{line.begin(), line.begin() + size};

		size_t start = 2;
		size_t end = 3;
		if(line_view[0] == 'v') {
			for(size_t i = 0; i < 3; ++i) {
				end = start + 1;
				while(end < line_view.size() && line_view[end] != ' ')
					++end;
				std::from_chars(line_view.data() + start, line_view.data() + end, v.pos[i]);
				start = end + 1;
			}
			_vertices.push_back(v);
		} else if(line_view[0] == 'f') {
			uint16_t coord;
			for(size_t i = 0; i < 3; ++i) { // Supports only triangles.
				end = start + 1;
				while(end < line_view.size() && line_view[end] != ' ')
					++end;
				std::from_chars(line_view.data() + start, line_view.data() + end, coord);
				_indices.push_back(coord - 1); // Indices starts at 1 in .obj
				start = end + 1;
			}
		} else {
			warn("Unsupported OBJ command: '{}' (Full line: '{}')\n", line_view[0], line);
		}
	}
	return true;
}

void Mesh::normalizeVertices() {
	glm::vec3 acc{0};
	for(const auto& v : _vertices)
		acc += v.pos;
	acc /= _vertices.size();
	for(auto& v : _vertices)
		v.pos -= acc;
}

void Mesh::computeVertexNormals() {
	// Here, normals are the average of adjacent triangles' normals
	// (so we have exactly one normal per vertex)
	for(auto& v : _vertices)
		v.normal = glm::vec3{0.0f};

	for(size_t i = 0; i < _indices.size();) {
		// Norm of this triangle
		glm::vec3 norm = glm::normalize(glm::cross(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos, _vertices[_indices[i + 2]].pos - _vertices[_indices[i]].pos));
		// Add it to each vertex
		for(size_t j = 0; j < 3; ++j) {
			_vertices[_indices[i]].normal += norm;
			++i;
		}
	}
	// Average norms
	for(auto& v : _vertices)
		v.normal = glm::normalize(v.normal);
}
