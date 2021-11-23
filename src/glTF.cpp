#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "JSON.hpp"
#include "Logger.hpp"

glTF::glTF(std::filesystem::path path) {
	load(path);
}

void glTF::load(std::filesystem::path path) {
	// TODO: Check for file extension (.gltf (json/ascii) or .glb)
	JSON json{path};
	fmt::print("{}\n", json);
	const auto& object = json.getRoot();

	// Load Buffers
	std::vector<std::vector<char>> buffers;
	for(const auto& bufferDesc : object["buffers"]) {
		buffers.push_back({});
		auto&		  buffer = buffers.back();
		auto		  filepath = path.parent_path().append(bufferDesc["uri"].asString());
		size_t		  length = bufferDesc["byteLength"].asNumber().asInteger();
		std::ifstream buffer_file{filepath};
		if(buffer_file) {
			buffer.resize(length);
			if(!buffer_file.read(buffer.data(), length)) {
				error("Error while reading '{}'.", filepath);
				return;
			}
		} else {
			error("Could not open '{}'.", filepath);
			return;
		}
	}

	for(const auto& m : object["meshes"]) {
		_meshes.push_back({});
		Mesh& mesh = _meshes.back();
		fmt::print("{}\n", m["name"]);

		const auto& primitives = m["primitives"];
		for(const auto& p : primitives) {
			Primitive primitive{
				.mode = static_cast<RenderingMode>(p["mode"].asNumber().asInteger()),
				.material = static_cast<size_t>(p["material"].asNumber().asInteger()),
			};
			const auto& positionAccessor = object["accessors"][p["attributes"]["POSITION"].asNumber().asInteger()];
			const auto& positionBufferView = object["bufferViews"][positionAccessor["bufferView"].asNumber().asInteger()];
			const auto& positionBuffer = buffers[positionBufferView["buffer"].asNumber().asInteger()];
			const auto& normalAccessor = object["accessors"][p["attributes"]["NORMAL"].asNumber().asInteger()];
			const auto& normalBufferView = object["bufferViews"][normalAccessor["bufferView"].asNumber().asInteger()];
			const auto& normalBuffer = buffers[normalBufferView["buffer"].asNumber().asInteger()];
			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].asNumber().asInteger()) == ComponentType::Float); // TODO
				assert(static_cast<ComponentType>(normalAccessor["componentType"].asNumber().asInteger()) == ComponentType::Float);	  // TODO
				size_t positionCursor = positionAccessor["byteOffset"].asNumber().asInteger() + positionBufferView["byteOffset"].asNumber().asInteger();
				size_t positionStride = positionBufferView.asObject().contains("byteStride") ? positionBufferView["byteStride"].asNumber().asInteger() : 3 * sizeof(float);

				size_t normalCursor = normalAccessor["byteOffset"].asNumber().asInteger() + normalBufferView["byteOffset"].asNumber().asInteger();
				size_t normalStride = normalBufferView.asObject().contains("byteStride") ? normalBufferView["byteStride"].asNumber().asInteger() : 3 * sizeof(float);

				assert(positionAccessor["count"].asNumber().asInteger() == normalAccessor["count"].asNumber().asInteger());
				for(size_t i = 0; i < positionAccessor["count"].asNumber().asInteger(); ++i) {
					Vertex v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
					v.pos[0] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor);
					v.pos[1] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor + sizeof(float));
					v.pos[2] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor + 2 * sizeof(float));
					positionCursor += positionStride;
					v.normal[0] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor);
					v.normal[1] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor + sizeof(float));
					v.normal[2] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor + 2 * sizeof(float));
					normalCursor += normalStride;
					mesh.getVertices().push_back(v);
				}
			} else {
				error("Error: Unsupported accessor type '{}'.", positionAccessor["type"].asString());
			}

			const auto& indicesAccessor = object["accessors"][p["indices"].asNumber().asInteger()];
			const auto& indicesBufferView = object["bufferViews"][indicesAccessor["bufferView"].asNumber().asInteger()];
			const auto& indicesBuffer = buffers[indicesBufferView["buffer"].asNumber().asInteger()];
			if(indicesAccessor["type"].asString() == "SCALAR") {
				assert(static_cast<ComponentType>(indicesAccessor["componentType"].asNumber().asInteger()) == ComponentType::UnsignedShort); // TODO
				size_t cursor = indicesAccessor["byteOffset"].asNumber().asInteger() + indicesBufferView["byteOffset"].asNumber().asInteger();
				size_t stride = indicesBufferView.asObject().contains("byteStride") ? indicesBufferView["byteStride"].asNumber().asInteger() : sizeof(unsigned short);
				for(size_t i = 0; i < indicesAccessor["count"].asNumber().asInteger(); ++i) {
					const unsigned short idx = *reinterpret_cast<const unsigned short*>(indicesBuffer.data() + cursor);
					cursor += stride;
					mesh.getIndices().push_back(idx);
				}

			} else {
				error("Error: Unsupported accessor type '{}'.", indicesAccessor["type"].asString());
			}
		}
	}
}

glTF::~glTF() {}
