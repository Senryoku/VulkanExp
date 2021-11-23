#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "JSON.hpp"

glTF::glTF(std::filesystem::path path) {
	// TODO: Check for file extension (.gltf (json/ascii) or .glb)
	JSON json{path};
	fmt::print("{}\n", json);
	const auto& object = json.getRoot();

	for(const auto& m : object["meshes"]) {
		fmt::print("{}\n", m["name"]);
		const auto& primitives = m["primitives"];
		for(const auto& p : primitives) {
			Primitive primitive{
				.mode = static_cast<RenderingMode>(p["mode"].asNumber().asInteger()),
				.material = static_cast<size_t>(p["material"].asNumber().asInteger()),
			};
			for(const auto& pair : p["attributes"].asObject()) {
				fmt::print("{}: {}\n", pair.first, pair.second);
			}
		}
	}
}

glTF::~glTF() {}
