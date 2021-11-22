#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "JSON.hpp"

glTF::glTF(std::filesystem::path path) {
	// TODO: Check for file extension (.gltf (json/ascii) or .glb)
	JSON json{path};
	fmt::print("{}\n", json);
	const auto& object = json.getRoot().asObject();

	if(object.contains("scene"))
		fmt::print("Ok!");
	for(const auto& m : object.at("meshes")) {
		fmt::print("{}\n", m.asObject().at("name"));
		const auto& primitives = m.asObject().at("primitives").asArray();
		for(const auto& p : primitives) {
			static_cast<RenderingMode>(p.asObject().at("mode").asNumber().asInteger());
		}
	}
}

glTF::~glTF() {}
