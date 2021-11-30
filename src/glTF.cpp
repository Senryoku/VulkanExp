#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "JSON.hpp"
#include "Logger.hpp"
#include "STBImage.hpp"
#include <vulkan/Image.hpp>
#include <vulkan/ImageView.hpp>
#include <vulkan/Material.hpp>

glTF::glTF(std::filesystem::path path) {
	load(path);
}

void glTF::load(std::filesystem::path path) {
	// TODO: Check for file extension (.gltf (json/ascii) or .glb)
	JSON		json{path};
	const auto& object = json.getRoot();

	// Load Buffers
	std::vector<std::vector<char>> buffers;
	for(const auto& bufferDesc : object["buffers"]) {
		buffers.push_back({});
		auto&		  buffer = buffers.back();
		auto		  filepath = path.parent_path() / bufferDesc["uri"].asString();
		size_t		  length = bufferDesc["byteLength"].as<int>();
		std::ifstream buffer_file{filepath, std::ifstream::binary};
		if(buffer_file) {
			buffer.resize(length);
			if(!buffer_file.read(buffer.data(), length)) {
				error("Error while reading '{}' (rdstate: {}, size: {} bytes).\n", filepath, buffer_file.rdstate(), length);
				return;
			}
		} else {
			error("Could not open '{}'.", filepath);
			return;
		}
	}

	for(const auto& mat : object["materials"]) {
		Material	material;
		const auto& texture = object["textures"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"].as<int>()];
		material.textures["baseColor"] = Material::Texture{
			.source = path.parent_path() / object["images"][texture["source"].as<int>()]["uri"].asString(),
			.samplerDescription = object["samplers"][texture["sampler"].as<int>()].asObject(),
		};
		Materials.push_back(material);
	}

	for(const auto& m : object["meshes"]) {
		const auto& primitives = m["primitives"];
		for(const auto& p : primitives) {
			_meshes.push_back({});
			Mesh& mesh = _meshes.back();
			if(p.asObject().contains("material")) {
				mesh.materialIndex = p["material"].as<int>();
				mesh.material = &Materials[p["material"].as<int>()];
			}

			Primitive primitive{
				.mode = static_cast<RenderingMode>(p["mode"].as<int>()),
				.material = static_cast<size_t>(p["material"].as<int>()),
			};
			const auto& positionAccessor = object["accessors"][p["attributes"]["POSITION"].as<int>()];
			const auto& positionBufferView = object["bufferViews"][positionAccessor["bufferView"].as<int>()];
			const auto& positionBuffer = buffers[positionBufferView["buffer"].as<int>()];

			const auto& normalAccessor = object["accessors"][p["attributes"]["NORMAL"].as<int>()];
			const auto& normalBufferView = object["bufferViews"][normalAccessor["bufferView"].as<int>()];
			const auto& normalBuffer = buffers[normalBufferView["buffer"].as<int>()];

			const auto& texCoordAccessor = object["accessors"][p["attributes"]["TEXCOORD_0"].as<int>()];
			const auto& texCoordBufferView = object["bufferViews"][texCoordAccessor["bufferView"].as<int>()];
			const auto& texCoordBuffer = buffers[texCoordBufferView["buffer"].as<int>()];

			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].as<int>()) == ComponentType::Float); // TODO
				assert(static_cast<ComponentType>(normalAccessor["componentType"].as<int>()) == ComponentType::Float);	 // TODO
				size_t positionCursor = positionAccessor["byteOffset"].as<int>() + positionBufferView["byteOffset"].as<int>();
				size_t positionStride = positionBufferView.asObject().contains("byteStride") ? positionBufferView["byteStride"].as<int>() : 3 * sizeof(float);

				size_t normalCursor = normalAccessor["byteOffset"].as<int>() + normalBufferView["byteOffset"].as<int>();
				size_t normalStride = normalBufferView.asObject().contains("byteStride") ? normalBufferView["byteStride"].as<int>() : 3 * sizeof(float);

				size_t texCoordCursor = texCoordAccessor["byteOffset"].as<int>() + texCoordBufferView["byteOffset"].as<int>();
				size_t texCoordStride = texCoordBufferView.asObject().contains("byteStride") ? texCoordBufferView["byteStride"].as<int>() : 2 * sizeof(float);

				assert(positionAccessor["count"].as<int>() == normalAccessor["count"].as<int>());
				for(size_t i = 0; i < positionAccessor["count"].as<int>(); ++i) {
					Vertex v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
					v.pos[0] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor);
					v.pos[1] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor + sizeof(float));
					v.pos[2] = *reinterpret_cast<const float*>(positionBuffer.data() + positionCursor + 2 * sizeof(float));
					positionCursor += positionStride;
					v.normal[0] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor);
					v.normal[1] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor + sizeof(float));
					v.normal[2] = *reinterpret_cast<const float*>(normalBuffer.data() + normalCursor + 2 * sizeof(float));
					normalCursor += normalStride;
					v.texCoord[0] = *reinterpret_cast<const float*>(texCoordBuffer.data() + texCoordCursor);
					v.texCoord[1] = *reinterpret_cast<const float*>(texCoordBuffer.data() + texCoordCursor + sizeof(float));
					texCoordCursor += texCoordStride;
					mesh.getVertices().push_back(v);
				}
			} else {
				error("Error: Unsupported accessor type '{}'.", positionAccessor["type"].asString());
			}

			const auto& indicesAccessor = object["accessors"][p["indices"].as<int>()];
			const auto& indicesBufferView = object["bufferViews"][indicesAccessor["bufferView"].as<int>()];
			const auto& indicesBuffer = buffers[indicesBufferView["buffer"].as<int>()];
			if(indicesAccessor["type"].asString() == "SCALAR") {
				assert(static_cast<ComponentType>(indicesAccessor["componentType"].as<int>()) == ComponentType::UnsignedShort); // TODO
				size_t cursor = indicesAccessor["byteOffset"].as<int>() + indicesBufferView["byteOffset"].as<int>();
				size_t stride = indicesBufferView.asObject().contains("byteStride") ? indicesBufferView["byteStride"].as<int>() : sizeof(unsigned short);
				for(size_t i = 0; i < indicesAccessor["count"].as<int>(); ++i) {
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