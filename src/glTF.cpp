#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>

#include "JSON.hpp"

glTF::glTF(std::filesystem::path path) {
    // TODO: Check for file extension (.gltf (json/ascii) or .glb)
    JSON json{path};
}

glTF::~glTF() {

}