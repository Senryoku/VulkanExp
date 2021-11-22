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

		auto tokens = split(line_view, " \t");

		if(tokens[0] == "v") {
			for(size_t i = 1; i < tokens.size(); ++i)
				std::from_chars(tokens[i].data(), tokens[i].data() + tokens[i].size(), v.pos[i - 1]);
			_vertices.push_back(v);
		} else if(tokens[0] == "f") {
			assert(tokens.size() == 4); // TODO: Only supports triangles rn
			uint16_t coords[3];
			for(size_t i = 1; i < tokens.size(); ++i) {
				std::from_chars(tokens[i].data(), tokens[i].data() + tokens[i].size(), coords[i - 1]);
				_indices.push_back(coords[i - 1] - 1); // Indices starts at 1 in .obj
			}
		} else {
			warn("Unsupported OBJ command: {} (Full line:{})\n", tokens[0], line);
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
