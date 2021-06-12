#include "Mesh.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <Logger.hpp>
#include <stringutils.hpp>

bool Mesh::loadOBJ(const std::filesystem::path& path) {
    std::ifstream file{path};

    _vertices.clear();
    _indices.clear();

    std::string line;
    while(std::getline(file, line)) {
        // Ignore empty lines and comments
        if(line.empty() || line[line.find_first_not_of(" \t")] == '#')
            continue;
        // Remove comments
        line = line.substr(0, line.find('#'));

        auto tokens = split(line, " \t");

        if(tokens[0] == "v") {
            glm::vec3 coords{0};
            for(size_t i = 0; i < 3; ++i)
                if(tokens.size() > i + 1)
                    std::from_chars(tokens[i + 1].data(), tokens[i + 1].data() + tokens[i + 1].size(), coords[i]);
            _vertices.push_back({coords, glm::vec3{1.0, 1.0, 1.0}});
        } else if(tokens[0] == "f") {
            assert(tokens.size() == 4); // TODO: Only supports triangles rn
            uint16_t coords[3];
            for(size_t i = 0; i < 3; ++i) {
                if(tokens.size() > i + 1) {
                    std::from_chars(tokens[i + 1].data(), tokens[i + 1].data() + tokens[i + 1].size(), coords[i]);
                    _indices.push_back(coords[i] - 1); // Indices starts at 1 in .obj
                }
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