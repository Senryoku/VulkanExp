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

template<>
const glm::vec4& JSON::value::as<glm::vec4>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 4);
	return *reinterpret_cast<const glm::vec4*>(_value.as_array.data());
}

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
		Material material;
		material.name = mat("name", std::string("NoName"));
		material.baseColorFactor = mat["pbrMetallicRoughness"]("baseColorFactor", glm::vec4{1.0, 1.0, 1.0, 1.0});
		material.metallicFactor = mat["pbrMetallicRoughness"]("metallicFactor", 1.0f);
		material.roughnessFactor = mat["pbrMetallicRoughness"]("roughnessFactor", 1.0f);
		const auto& texture = object["textures"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"].as<int>()];
		material.textures["baseColor"] = Material::Texture{
			.source = path.parent_path() / object["images"][texture["source"].as<int>()]["uri"].asString(),
			.samplerDescription = object["samplers"][texture["sampler"].as<int>()].asObject(),
		};
		if(mat.contains("normalTexture")) {
			const auto& normalTexture = object["textures"][mat["normalTexture"]["index"].as<int>()];
			material.textures["normal"] = Material::Texture{
				.source = path.parent_path() / object["images"][normalTexture["source"].as<int>()]["uri"].asString(),
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.samplerDescription = object["samplers"][normalTexture["sampler"].as<int>()].asObject(),
			};
		}
		if(mat["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
			const auto& metallicRoughnessTexture = object["textures"][mat["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"].as<int>()];
			material.textures["metallicRoughness"] = Material::Texture{
				.source = path.parent_path() / object["images"][metallicRoughnessTexture["source"].as<int>()]["uri"].asString(),
				.samplerDescription = object["samplers"][metallicRoughnessTexture["sampler"].as<int>()].asObject(),
			};
		}
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
				.mode = static_cast<RenderingMode>(p.asObject().contains("mode") ? p["mode"].as<int>() : 0),
				.material = static_cast<size_t>(p["material"].as<int>()),
			};
			const auto& positionAccessor = object["accessors"][p["attributes"]["POSITION"].as<int>()];
			const auto& positionBufferView = object["bufferViews"][positionAccessor["bufferView"].as<int>()];
			const auto& positionBuffer = buffers[positionBufferView["buffer"].as<int>()];

			const auto& normalAccessor = object["accessors"][p["attributes"]["NORMAL"].as<int>()];
			const auto& normalBufferView = object["bufferViews"][normalAccessor["bufferView"].as<int>()];
			const auto& normalBuffer = buffers[normalBufferView["buffer"].as<int>()];

			const char* tangentBufferData = nullptr;
			size_t		tangentCursor = 0;
			size_t		tangentStride = 0;
			if(p["attributes"].contains("TANGENT")) {
				const auto& tangentAccessor = object["accessors"][p["attributes"]["TANGENT"].as<int>()];
				assert(static_cast<ComponentType>(tangentAccessor["componentType"].as<int>()) == ComponentType::Float); // Supports only vec4 of floats
				assert(tangentAccessor["type"].as<std::string>() == "VEC4");
				const auto& tangentBufferView = object["bufferViews"][tangentAccessor["bufferView"].as<int>()];
				const auto& tangentBuffer = buffers[tangentBufferView["buffer"].as<int>()];
				tangentBufferData = tangentBuffer.data();
				tangentCursor = tangentAccessor("byteOffset", 0) + tangentBufferView("byteOffset", 0);
				const int defaultStride = 4 * sizeof(float);
				tangentStride = tangentBufferView("byteStride", defaultStride);
			}
			// TODO: Compute tangents if not present in file.

			const auto& texCoordAccessor = object["accessors"][p["attributes"]["TEXCOORD_0"].as<int>()];
			const auto& texCoordBufferView = object["bufferViews"][texCoordAccessor["bufferView"].as<int>()];
			const auto& texCoordBuffer = buffers[texCoordBufferView["buffer"].as<int>()];

			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].as<int>()) == ComponentType::Float); // TODO
				assert(static_cast<ComponentType>(normalAccessor["componentType"].as<int>()) == ComponentType::Float);	 // TODO
				size_t positionCursor = positionAccessor("byteOffset", 0) + positionBufferView("byteOffset", 0);
				size_t positionStride = positionBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				size_t normalCursor = normalAccessor("byteOffset", 0) + normalBufferView("byteOffset", 0);
				size_t normalStride = normalBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				size_t texCoordCursor = texCoordAccessor("byteOffset", 0) + texCoordBufferView("byteOffset", 0);
				size_t texCoordStride = texCoordBufferView("byteStride", static_cast<int>(2 * sizeof(float)));

				assert(positionAccessor["count"].as<int>() == normalAccessor["count"].as<int>());
				for(size_t i = 0; i < positionAccessor["count"].as<int>(); ++i) {
					Vertex v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
					v.pos = *reinterpret_cast<const glm::vec3*>(positionBuffer.data() + positionCursor);
					positionCursor += positionStride;

					v.normal = *reinterpret_cast<const glm::vec3*>(normalBuffer.data() + normalCursor);
					normalCursor += normalStride;

					if(tangentBufferData) {
						v.tangent = *reinterpret_cast<const glm::vec4*>(tangentBufferData + tangentCursor);
						assert(v.tangent.w == -1.0f || v.tangent.w == 1.0f);
						tangentCursor += tangentStride;
					} else
						v.tangent = glm::vec4(1.0);

					v.texCoord = *reinterpret_cast<const glm::vec2*>(texCoordBuffer.data() + texCoordCursor);
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
				size_t cursor = indicesAccessor("byteOffset", 0) + indicesBufferView("byteOffset", 0);
				size_t stride = indicesBufferView("byteStride", static_cast<int>(sizeof(unsigned short)));
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
