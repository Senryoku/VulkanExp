#include "glTF.hpp"

#include <fstream>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "JSON.hpp"
#include "Logger.hpp"
#include "STBImage.hpp"
#include <vulkan/Image.hpp>
#include <vulkan/ImageView.hpp>
#include <vulkan/Material.hpp>

template<>
glm::vec3 JSON::value::to<glm::vec3>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 3);
	const auto& a = _value.as_array;
	return glm::vec3{a[0].to<float>(), a[1].to<float>(), a[2].to<float>()};
}

template<>
glm::vec4 JSON::value::to<glm::vec4>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 4);
	const auto& a = _value.as_array;
	return glm::vec4{
		a[0].to<float>(),
		a[1].to<float>(),
		a[2].to<float>(),
		a[3].to<float>(),
	};
}

template<>
glm::quat JSON::value::to<glm::quat>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 4);
	const auto& a = _value.as_array;
	return glm::quat{
		a[0].to<float>(),
		a[1].to<float>(),
		a[2].to<float>(),
		a[3].to<float>(),
	};
}

template<>
glm::mat4 JSON::value::to<glm::mat4>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 16);
	const auto& a = _value.as_array;
	return glm::mat4{
		a[0].to<float>(), a[1].to<float>(), a[2].to<float>(),  a[3].to<float>(),  a[4].to<float>(),	 a[5].to<float>(),	a[6].to<float>(),  a[7].to<float>(),
		a[8].to<float>(), a[9].to<float>(), a[10].to<float>(), a[11].to<float>(), a[12].to<float>(), a[13].to<float>(), a[14].to<float>(), a[15].to<float>(),
	};
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

	if(object.contains("textures"))
		for(const auto& texture : object["textures"]) {
			Textures.push_back(Texture{
				.source = path.parent_path() / object["images"][texture["source"].as<int>()]["uri"].asString(),
				.format = VK_FORMAT_R8G8B8A8_UNORM, // FIXME: Use this for normal maps only! (or not?)
				.samplerDescription = object["samplers"][texture["sampler"].as<int>()].asObject(),
			});
		}

	if(object.contains("materials"))
		for(const auto& mat : object["materials"]) {
			Material material;
			material.name = mat("name", std::string("NoName"));
			if(mat.contains("pbrMetallicRoughness")) {
				material.baseColorFactor = mat["pbrMetallicRoughness"].get("baseColorFactor", glm::vec4{1.0, 1.0, 1.0, 1.0});
				material.metallicFactor = mat["pbrMetallicRoughness"].get("metallicFactor", 0.0f);
				material.roughnessFactor = mat["pbrMetallicRoughness"].get("roughnessFactor", 1.0f);
				if(mat["pbrMetallicRoughness"].contains("baseColorTexture")) {
					material.albedoTexture = mat["pbrMetallicRoughness"]["baseColorTexture"]["index"].as<int>();
				}
				if(mat["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
					material.metallicRoughnessTexture = mat["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"].as<int>();
				}
			}
			material.emissiveFactor = mat.get("emissiveFactor", glm::vec3(0.0f));
			if(mat.contains("emissiveTexture")) {
				material.emissiveTexture = mat["emissiveTexture"]["index"].as<int>();
			}
			if(mat.contains("normalTexture")) {
				material.normalTexture = mat["normalTexture"]["index"].as<int>();
			}
			Materials.push_back(material);
		}

	for(const auto& m : object["meshes"]) {
		auto& mesh = _meshes.emplace_back();
		mesh.name = m("name", std::string("NoName"));
		for(const auto& p : m["primitives"]) {
			auto& submesh = mesh.SubMeshes.emplace_back();
			submesh.name = m("name", std::string("NoName"));
			if(p.asObject().contains("material")) {
				submesh.materialIndex = p["material"].as<int>();
				submesh.material = &Materials[p["material"].as<int>()];
			}

			if(p.contains("mode"))
				assert(p["mode"].as<int>() == 4); // We only supports triangles
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
					submesh.getVertices().push_back(v);
				}
			} else {
				error("Error: Unsupported accessor type '{}'.", positionAccessor["type"].asString());
			}

			if(positionAccessor.contains("min") && positionAccessor.contains("max")) {
				submesh.setBounds({
					.min = positionAccessor["min"].to<glm::vec3>(),
					.max = positionAccessor["max"].to<glm::vec3>(),
				});
			} else {
				submesh.computeBounds();
			}

			const auto& indicesAccessor = object["accessors"][p["indices"].as<int>()];
			const auto& indicesBufferView = object["bufferViews"][indicesAccessor["bufferView"].as<int>()];
			const auto& indicesBuffer = buffers[indicesBufferView["buffer"].as<int>()];
			if(indicesAccessor["type"].asString() == "SCALAR") {
				ComponentType compType = static_cast<ComponentType>(indicesAccessor["componentType"].as<int>());
				assert(compType == ComponentType::UnsignedShort || compType == ComponentType::UnsignedInt); // TODO
				size_t cursor = indicesAccessor("byteOffset", 0) + indicesBufferView("byteOffset", 0);
				int	   defaultStride = 0;
				switch(compType) {
					case ComponentType::UnsignedShort: defaultStride = sizeof(unsigned short); break;
					case ComponentType::UnsignedInt: defaultStride = sizeof(unsigned int); break;
					default: assert(false);
				}
				size_t stride = indicesBufferView("byteStride", defaultStride);
				for(size_t i = 0; i < indicesAccessor["count"].as<int>(); ++i) {
					uint32_t idx = 0;
					switch(compType) {
						case ComponentType::UnsignedShort: idx = *reinterpret_cast<const unsigned short*>(indicesBuffer.data() + cursor); break;
						case ComponentType::UnsignedInt: idx = *reinterpret_cast<const unsigned int*>(indicesBuffer.data() + cursor); break;
						default: assert(false);
					}
					idx = *reinterpret_cast<const unsigned short*>(indicesBuffer.data() + cursor);
					submesh.getIndices().push_back(idx);
					cursor += stride;
				}

			} else {
				error("Error: Unsupported accessor type '{}'.", indicesAccessor["type"].asString());
			}
		}
	}

	for(const auto& node : object["nodes"]) {
		Node n;
		n.name = node("name", std::string("Unamed Node"));
		if(node.contains("matrix")) {
			n.transform = node["matrix"].to<glm::mat4>();
		} else {
			auto scale = glm::scale(glm::mat4(1.0f), node.get("scale", glm::vec3(1.0)));
			auto rotation = glm::toMat4(node.get("rotation", glm::quat(1, 0, 0, 0)));
			auto translation = glm::translate(glm::mat4(1.0f), node.get("translation", glm::vec3(0.0f)));
			n.transform = translation * rotation * scale;
		}

		if(node.contains("children")) {
			for(const auto& c : node["children"]) {
				n.children.push_back(c.as<int>());
			}
		}
		n.mesh = node("mesh", -1);
		_nodes.push_back(n);
	}

	for(const auto& scene : object["scenes"]) {
		Scene s;
		s.name = scene("name", std::string("Unamed Scene"));
		if(scene.contains("nodes")) {
			for(const auto& c : scene["nodes"]) {
				s.nodes.push_back(c.as<int>());
			}
		}
		_scenes.push_back(s);
	}

	_defaultScene = object("scene", 0);
}

void glTF::allocateMeshes(const Device& device) {
	uint32_t						  totalVertexSize = 0;
	uint32_t						  totalIndexSize = 0;
	std::vector<VkMemoryRequirements> memReqs;
	std::vector<OffsetEntry>		  offsetTable;
	for(const auto& m : getMeshes()) {
		for(const auto& sm : m.SubMeshes) {
			auto vertexBufferMemReq = sm.getVertexBuffer().getMemoryRequirements();
			auto indexBufferMemReq = sm.getIndexBuffer().getMemoryRequirements();
			memReqs.push_back(vertexBufferMemReq);
			memReqs.push_back(indexBufferMemReq);
			offsetTable.push_back(OffsetEntry{static_cast<uint32_t>(sm.materialIndex), totalVertexSize / static_cast<uint32_t>(sizeof(Vertex)),
											  totalIndexSize / static_cast<uint32_t>(sizeof(uint32_t))});
			totalVertexSize += static_cast<uint32_t>(vertexBufferMemReq.size);
			totalIndexSize += static_cast<uint32_t>(indexBufferMemReq.size);
		}
	}
	VertexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[0].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalVertexSize);
	IndexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[1].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalIndexSize);
	size_t submeshIdx = 0;
	for(const auto& m : getMeshes()) {
		for(const auto& sm : m.SubMeshes) {
			vkBindBufferMemory(device, sm.getVertexBuffer(), VertexMemory, NextVertexMemoryOffset);
			NextVertexMemoryOffset += memReqs[2 * submeshIdx].size;
			vkBindBufferMemory(device, sm.getIndexBuffer(), IndexMemory, NextIndexMemoryOffset);
			NextIndexMemoryOffset += memReqs[2 * submeshIdx + 1].size;
			++submeshIdx;
		}
	}
	// Create views to the entire dataset
	VertexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalVertexSize);
	vkBindBufferMemory(device, VertexBuffer, VertexMemory, 0);
	IndexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalIndexSize);
	vkBindBufferMemory(device, IndexBuffer, IndexMemory, 0);

	OffsetTableSize = static_cast<uint32_t>(sizeof(OffsetEntry) * offsetTable.size());
	OffsetTableBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, OffsetTableSize);
	OffsetTableMemory.allocate(device, OffsetTableBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	// FIXME: Remove VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT and use a staging buffer.
	OffsetTableMemory.fill(offsetTable.data(), offsetTable.size());
}

void glTF::free() {
	for(auto& m : getMeshes())
		m.destroy();

	OffsetTableBuffer.destroy();
	IndexBuffer.destroy();
	VertexBuffer.destroy();
	OffsetTableMemory.free();
	VertexMemory.free();
	IndexMemory.free();
}

glTF::~glTF() {}
