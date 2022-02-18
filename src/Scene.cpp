#include "Scene.hpp"

#include <fstream>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "JSON.hpp"
#include "Logger.hpp"
#include "STBImage.hpp"
#include <Base64.hpp>
#include <QuickTimer.hpp>
#include <Serialization.hpp>
#include <vulkan/CommandBuffer.hpp>
#include <vulkan/Image.hpp>
#include <vulkan/ImageView.hpp>
#include <vulkan/Material.hpp>

Scene::Scene() {
	_registry.on_destroy<NodeComponent>().connect<&Scene::onDestroyNodeComponent>(this);
	_root = _registry.create();
	_registry.emplace<NodeComponent>(_root);
}

Scene::Scene(const std::filesystem::path& path) {
	load(path);
}

Scene::~Scene() {}

bool Scene::load(const std::filesystem::path& path) {
	const auto canonicalPath = path.is_absolute() ? path.lexically_relative(std::filesystem::current_path()) : path.lexically_normal();
	const auto ext = path.extension();
	if(ext == ".scene")
		return loadScene(canonicalPath);
	if(ext == ".obj")
		return loadOBJ(canonicalPath);
	else if(ext == ".gltf" || ext == ".glb")
		return loadglTF(canonicalPath);
	else
		warn("Scene::load: File extension {} is not supported ('{}').", ext, path.string());
	return false;
}

struct GLBHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t length;
};

enum class GLBChunkType : uint32_t {
	JSON = 0x4E4F534A,
	BIN = 0x004E4942,
};

struct GLBChunk {
	uint32_t	 length;
	GLBChunkType type;
	char*		 data;
};

bool Scene::loadglTF(const std::filesystem::path& path) {
	JSON						   json;
	std::vector<std::vector<char>> buffers;

	if(path.extension() == ".gltf") {
		json.parse(path);
		// Load Buffers
		const auto& obj = json.getRoot(); // FIXME: Assigning a vector::iterator to our JSON::value::iterator union causes a reading violation in _Orphan_me_unlocked_v3 (in debug
										  // mode only). Maybe I'm doing something terribly wrong, but it looks a lot like a bug in MSVC stblib implementation. Relevant discussion:
										  // https://stackoverflow.com/questions/32748870/visual-studio-const-iterator-assignment-error, however in this case the crash happens on
										  // non-singular values (.begin() on a non-empty vector). const_iterator doesn't seem to be affected by this problem, so I'm creating a
										  // const alias to force its use as a workaround.
		for(const auto& bufferDesc : obj["buffers"]) {
			buffers.push_back({});
			auto&	   buffer = buffers.back();
			size_t	   length = bufferDesc["byteLength"].as<int>();
			const auto uri = bufferDesc["uri"].asString();
			if(uri.starts_with("data:")) {
				// Inlined data
				if(uri.starts_with("data:application/octet-stream;base64,")) {
					const auto data = std::string_view(uri).substr(std::string("data:application/octet-stream;base64,").size());
					buffer = std::move(decodeBase64(data));
				} else {
					warn("Scene::loadglTF: Unsupported data format ('{}'...)\n", uri.substr(0, 64));
					return false;
				}
			} else {
				// Load from file
				auto		  filepath = path.parent_path() / uri;
				std::ifstream buffer_file{filepath, std::ifstream::binary};
				if(buffer_file) {
					buffer.resize(length);
					if(!buffer_file.read(buffer.data(), length)) {
						error("Error while reading '{}' (rdstate: {}, size: {} bytes).\n", filepath, buffer_file.rdstate(), length);
						return false;
					}
				} else {
					error("Could not open '{}'.", filepath);
					return false;
				}
			}
		}
	} else if(path.extension() == ".glb") {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if(!file) {
			error("Scene::loadglTf error: Could not open file '{}'.\n", path.string());
			return false;
		}
		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<char> buffer(size);
		if(file.read(buffer.data(), size)) {
			GLBHeader header = *reinterpret_cast<GLBHeader*>(buffer.data());
			assert(header.magic == 0x46546C67);
			GLBChunk jsonChunk = *reinterpret_cast<GLBChunk*>(buffer.data() + sizeof(GLBHeader));
			assert(jsonChunk.type == GLBChunkType::JSON);
			jsonChunk.data = buffer.data() + sizeof(GLBHeader) + offsetof(GLBChunk, data);
			if(!json.parse(jsonChunk.data, jsonChunk.length)) {
				error("Scene::loadglTF: GLB ('{}') JSON chunk could not be parsed.\n", path.string());
				return false;
			}
			char* offset = buffer.data() + sizeof(GLBHeader) + offsetof(GLBChunk, data) + jsonChunk.length;
			while(offset - buffer.data() < header.length) {
				GLBChunk chunk = *reinterpret_cast<GLBChunk*>(offset);
				assert(chunk.type == GLBChunkType::BIN);
				chunk.data = offset + offsetof(GLBChunk, data);
				offset += offsetof(GLBChunk, data) + chunk.length;
				buffers.emplace_back().assign(chunk.data, chunk.data + chunk.length);
			}
		}
	} else {
		warn("Scene::loadglTF: Extension '{}' not supported (filepath: '{}').", path.extension(), path.string());
		return false;
	}
	const auto& object = json.getRoot();

	const auto textureOffset = Textures.size();

	loadTextures(path, object);

	const auto materialOffset = Materials.size();

	if(object.contains("materials"))
		for(const auto& mat : object["materials"])
			loadMaterial(mat, textureOffset);

	const MeshIndex meshOffset{static_cast<MeshIndex::UnderlyingType>(_meshes.size())};

	for(const auto& m : object["meshes"]) {
		auto& mesh = _meshes.emplace_back();
		mesh.path = path;
		mesh.name = m("name", std::string("NoName"));
		for(const auto& p : m["primitives"]) {
			auto& submesh = mesh.SubMeshes.emplace_back();
			submesh.name = m("name", std::string("NoName"));
			if(p.asObject().contains("material")) {
				submesh.materialIndex.value = materialOffset + p["material"].as<int>();
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

			const char* texCoordBufferData = nullptr;
			size_t		texCoordCursor = 0;
			size_t		texCoordStride = 0;
			if(p["attributes"].contains("TEXCOORD_0")) {
				const auto& texCoordAccessor = object["accessors"][p["attributes"]["TEXCOORD_0"].as<int>()];
				const auto& texCoordBufferView = object["bufferViews"][texCoordAccessor["bufferView"].as<int>()];
				const auto& texCoordBuffer = buffers[texCoordBufferView["buffer"].as<int>()];
				texCoordBufferData = texCoordBuffer.data();
				texCoordCursor = texCoordAccessor("byteOffset", 0) + texCoordBufferView("byteOffset", 0);
				const int defaultStride = 2 * sizeof(float);
				texCoordStride = texCoordBufferView("byteStride", defaultStride);
			}

			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].as<int>()) == ComponentType::Float); // TODO
				assert(static_cast<ComponentType>(normalAccessor["componentType"].as<int>()) == ComponentType::Float);	 // TODO
				size_t positionCursor = positionAccessor("byteOffset", 0) + positionBufferView("byteOffset", 0);
				size_t positionStride = positionBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				size_t normalCursor = normalAccessor("byteOffset", 0) + normalBufferView("byteOffset", 0);
				size_t normalStride = normalBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				assert(positionAccessor["count"].as<int>() == normalAccessor["count"].as<int>());
				submesh.getVertices().reserve(positionAccessor["count"].as<int>());
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

					if(texCoordBufferData) {
						v.texCoord = *reinterpret_cast<const glm::vec2*>(texCoordBufferData + texCoordCursor);
						texCoordCursor += texCoordStride;
					}

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
			if(!submesh.getBounds().isValid())
				submesh.computeBounds();
			assert(submesh.getBounds().isValid());

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
				submesh.getIndices().reserve(indicesAccessor["count"].as<int>());
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

	std::vector<entt::entity>	  entities;
	std::vector<std::vector<int>> entitiesChildren;
	entities.reserve(object["nodes"].asArray().size());
	entitiesChildren.reserve(object["nodes"].asArray().size());

	for(const auto& node : object["nodes"]) {
		auto entity = _registry.create();
		entities.push_back(entity);
		entitiesChildren.emplace_back();
		auto& n = _registry.emplace<NodeComponent>(entity);
		n.name = node("name", std::string("Unamed Node"));
		if(node.contains("matrix")) {
			n.transform = node["matrix"].to<glm::mat4>();
		} else {
			auto scale = glm::scale(glm::mat4(1.0f), node.get("scale", glm::vec3(1.0)));
			auto rotation = glm::toMat4(node.get("rotation", glm::quat(1, 0, 0, 0)));
			auto translation = glm::translate(glm::mat4(1.0f), node.get("translation", glm::vec3(0.0f)));
			n.transform = translation * rotation * scale;
		}

		if(node.contains("children"))
			for(const auto& c : node["children"])
				entitiesChildren.back().push_back(c.as<int>());

		auto meshIndex = node("mesh", -1);
		if(meshIndex != -1)
			_registry.emplace<MeshComponent>(entity, MeshComponent{.index = MeshIndex(meshIndex)});
	}

	for(const auto& scene : object["scenes"]) {
		auto entity = _registry.create();
		entities.push_back(entity);
		entitiesChildren.emplace_back();
		auto& node = _registry.emplace<NodeComponent>(entity);
		addChild(_root, entity);
		node.name = scene("name", std::string("Unamed Scene"));
		if(scene.contains("nodes"))
			for(const auto& n : scene["nodes"])
				entitiesChildren.back().push_back(n.as<int>());
	}

	// Update nodes relationships now that they're all available
	for(size_t entityIndex = 0; entityIndex < entitiesChildren.size(); ++entityIndex) {
		for(const auto& childIdx : entitiesChildren[entityIndex])
			addChild(entities[entityIndex], entities[childIdx]);
	}

	computeBounds();  // FIXME?
	markDirty(_root); // Also probably unnecessary

	return true;
}

bool Scene::loadOBJ(const std::filesystem::path& path) {
	std::ifstream file{path};
	if(!file) {
		error("Scene::loadOBJ Error: Couldn't open '{}'.\n", path);
		return false;
	}

	Mesh*	 m = nullptr;
	SubMesh* sm = nullptr;

	auto  rootEntity = _registry.create();
	auto& n = _registry.emplace<NodeComponent>(rootEntity);
	_registry.emplace<MeshComponent>(rootEntity, MeshIndex{static_cast<MeshIndex>(_meshes.size() - 1)});
	addChild(_root, rootEntity);

	std::vector<glm::vec2> uvs;
	std::vector<glm::vec3> normals;
	uint32_t			   vertexOffset = 0;

	const auto nextMesh = [&]() {
		_meshes.emplace_back();
		m = &_meshes.back();

		auto entity = _registry.create();
		_registry.emplace<MeshComponent>(entity, MeshIndex{static_cast<MeshIndex>(_meshes.size() - 1)});
		addChild(rootEntity, entity);

		m->SubMeshes.push_back({});
		sm = &m->SubMeshes.back();
	};
	const auto nextSubMesh = [&]() {
		if(sm)
			vertexOffset += static_cast<uint32_t>(sm->getVertices().size());
		m->SubMeshes.push_back({});
		sm = &m->SubMeshes.back();
	};

	nextMesh();

	std::string line;
	Vertex		v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
	while(std::getline(file, line)) {
		// Ignore empty lines and comments
		if(line.empty())
			continue;
		auto it = line.find_first_not_of(" \t");
		if(it == std::string::npos || line[it] == '#')
			continue;
		if(line[0] == 'v') {
			if(line[1] == 't') {
				// Texture Coordinates
				char*	  cur = line.data() + 3;
				glm::vec2 uv;
				for(glm::vec2::length_type i = 0; i < 2; ++i)
					uv[i] = static_cast<float>(std::strtof(cur, &cur));
				uvs.push_back(uv);
			} else if(line[1] == 'n') {
				// Normals
				char*	  cur = line.data() + 3;
				glm::vec3 n;
				for(glm::vec3::length_type i = 0; i < 3; ++i)
					n[i] = static_cast<float>(std::strtof(cur, &cur));
				normals.push_back(glm::normalize(n));
			} else {
				// Vertices
				char* cur = line.data() + 2;
				for(glm::vec3::length_type i = 0; i < 3; ++i)
					v.pos[i] = static_cast<float>(std::strtof(cur, &cur));
				sm->getVertices().push_back(v);
			}
		} else if(line[0] == 'f') {
			char* cur = line.data() + 2;
			for(size_t i = 0; i < 3; ++i) { // Supports only triangles.
				auto vertexIndex = static_cast<uint32_t>(std::strtol(cur, &cur, 10));
				assert(vertexIndex > 0 && vertexIndex != std::numeric_limits<long int>::max());
				vertexIndex -= 1;			 // Indices starts at 1 in .obj
				vertexIndex -= vertexOffset; // Indices are absolutes in the obj file, we're relative to the current submesh
				sm->getIndices().push_back(vertexIndex);
				// Assuming vertices, uvs and normals have already been defined.
				if(*cur == '/') {
					++cur;
					// Texture coordinate index
					auto uvIndex = std::strtol(cur, &cur, 10);
					if(uvIndex > 0 && uvIndex != std::numeric_limits<long int>::max())
						sm->getVertices()[vertexIndex].texCoord = uvs[uvIndex - 1];
					// Normal index
					if(*cur == '/') {
						++cur;
						auto normalIndex = std::strtol(cur, &cur, 10);
						if(normalIndex > 0 && normalIndex != std::numeric_limits<long int>::max())
							sm->getVertices()[vertexIndex].normal = normals[normalIndex - 1];
					}
				}
			}
		} else if(line[0] == 'o') {
			// Next Mesh
			if(sm->getVertices().size() > 0)
				nextMesh();
		} else if(line[0] == 'g') {
			// Next SubMesh
			if(sm->getVertices().size() > 0) {
				sm->computeVertexNormals();
				sm->computeBounds();
				nextSubMesh();
			}
		} else {
			warn("Unsupported OBJ command: '{}' (Full line: '{}')\n", line[0], line);
		}
	}
	// FIXME: Something's broken (see Raytracing debug view: Almost all black)
	sm->computeVertexNormals();
	sm->computeBounds();
	return true;
}

bool Scene::loadMaterial(const std::filesystem::path& path) {
	if(path.extension() != ".mat") {
		warn("Scene::loadMaterial: Extension '{}' not supported (filepath: '{}').", path.extension(), path.string());
		return false;
	}
	JSON		json{path};
	const auto& object = json.getRoot();

	const auto textureOffset = static_cast<uint32_t>(Textures.size());

	loadTextures(path, object);

	if(object.contains("materials"))
		for(const auto& mat : object["materials"])
			loadMaterial(mat, textureOffset);

	return true;
}

bool Scene::loadMaterial(const JSON::value& mat, uint32_t textureOffset) {
	Material material = parseMaterial(mat, textureOffset);
	// Change the default format of this texture now that we know it will be used as a normal map
	if(material.properties.normalTexture != InvalidTextureIndex)
		if(material.properties.normalTexture < Textures.size())
			Textures[material.properties.normalTexture].format = VK_FORMAT_R8G8B8A8_UNORM;
		else
			warn("Scene::loadMaterial: Material '{}' refers to an out-of-bounds normal texture ({}).\n", material.name, material.properties.normalTexture);
	// Change the default format of this texture now that we know it will be used as a metallicRoughnessTexture
	if(material.properties.metallicRoughnessTexture != InvalidTextureIndex)
		if(material.properties.metallicRoughnessTexture < Textures.size())
			Textures[material.properties.metallicRoughnessTexture].format = VK_FORMAT_R8G8B8A8_UNORM;
		else
			warn("Scene::loadMaterial: Material '{}' refers to an out-of-bounds metallicRoughness texture ({}).\n", material.name, material.properties.metallicRoughnessTexture);
	Materials.push_back(material);
	return true;
}

bool Scene::loadTextures(const std::filesystem::path& path, const JSON::value& json) {
	if(json.contains("textures"))
		for(const auto& texture : json["textures"]) {
			Textures.push_back(Texture{
				.source = path.parent_path() / json["images"][texture["source"].as<int>()]["uri"].asString(),
				.format = VK_FORMAT_R8G8B8A8_SRGB,
				.samplerDescription = json["samplers"][texture("sampler", 0)].asObject(), // When undefined, a sampler with repeat wrapping and auto filtering should be used.
			});
		}
	return true;
}

bool Scene::save(const std::filesystem::path& path) {
	QuickTimer qt(fmt::format("Save Scene to '{}'", path.string()));
	JSON	   serialized{
		  {"materials", JSON::array()},
		  {"nodes", JSON::array()},
		  {"meshes", JSON::array()},
	  };
	auto& root = serialized.getRoot();

	auto& mats = root["materials"].asArray();
	for(const auto& mat : Materials)
		mats.push_back(toJSON(mat));

	auto& nodes = root["entities"].asArray();
	auto  view = _registry.view<NodeComponent>();
	for(const auto& entity : view) {
		auto& n = _registry.get<NodeComponent>(entity);
		JSON  nodeJSON{
			 {"name", n.name},
			 {"transform", toJSON(n.transform)},
			 {"parent", -1},
			 {"children", JSON::array()},
		 };
		if(auto* mesh = _registry.try_get<MeshComponent>(entity); mesh != nullptr) {
			nodeJSON["mesh"] = static_cast<int>(mesh->index);
		}
	}

	std::vector<GLBChunk> buffers;
	buffers.push_back({
		.type = GLBChunkType::JSON,
	}); // This will become the main JSON chunk header
	auto& meshes = root["meshes"].asArray();
	for(const auto& m : _meshes) {
		JSON mesh{
			{"name", m.name},
			{"submeshes", JSON::array()},
		};
		auto& submeshes = mesh["submeshes"].asArray();
		for(const auto& sm : m.SubMeshes) {
			int	 offset = static_cast<int>(buffers.size());
			JSON submesh{
				{"name", sm.name},
				{"material", sm.materialIndex.value},
				{"vertexArray", offset + 0},
				{"indexArray", offset + 1},
			};
			buffers.push_back(GLBChunk{static_cast<uint32_t>(sm.getVertexByteSize()), GLBChunkType::BIN, reinterpret_cast<char*>(const_cast<Vertex*>(sm.getVertices().data()))});
			buffers.push_back(GLBChunk{static_cast<uint32_t>(sm.getIndexByteSize()), GLBChunkType::BIN, reinterpret_cast<char*>(const_cast<uint32_t*>(sm.getIndices().data()))});
			submeshes.push_back(std::move(submesh));
		}
		meshes.push_back(std::move(mesh));
	}

	root["textures"] = JSON::array();
	auto& textures = root["textures"].asArray();
	for(const auto& t : Textures) {
		textures.push_back(JSON{
			{"source", t.source.lexically_relative(path.parent_path()).string()},
			{"format", static_cast<int>(t.format)},
			{"sampler", t.samplerDescription},
		});
	}

	std::ofstream file(path, std::ios::binary);
	if(!file) {
		error("Scene::save error: Could not open '{}' file for writing.", path.string());
		return false;
	}

	auto jsonStr = root.toString();
	buffers[0].length = static_cast<uint32_t>(jsonStr.size());
	buffers[0].data = jsonStr.data();

	uint32_t totalLength = sizeof(GLBHeader);
	for(const auto& buff : buffers)
		totalLength += 2 * sizeof(uint32_t) + buff.length;

	GLBHeader header{
		.magic = 0x4e454353,
		.version = 0,
		.length = totalLength,
	};

	file.write(reinterpret_cast<char*>(&header), sizeof(header));
	for(const auto& buff : buffers) {
		file.write(reinterpret_cast<const char*>(&buff.length), sizeof(buff.length));
		file.write(reinterpret_cast<const char*>(&buff.type), sizeof(buff.type));
		file.write(buff.data, buff.length);
	}

	file.close();

	return true;
}

bool Scene::loadScene(const std::filesystem::path& path) {
	QuickTimer qt(fmt::format("Loading Scene '{}'", path.string()));

	// FIXME
	Materials.clear();
	Textures.clear();
	_meshes.clear();

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if(!file) {
		error("Scene::loadScene error: Could not open file '{}'.\n", path.string());
		return false;
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<char>			   filebuffer(size);
	std::vector<std::vector<char>> buffers;
	if(file.read(filebuffer.data(), size)) {
		JSON	  json;
		GLBHeader header = *reinterpret_cast<GLBHeader*>(filebuffer.data());
		assert(header.magic == 0x4e454353);
		GLBChunk jsonChunk = *reinterpret_cast<GLBChunk*>(filebuffer.data() + sizeof(GLBHeader));
		assert(jsonChunk.type == GLBChunkType::JSON);
		jsonChunk.data = filebuffer.data() + sizeof(GLBHeader) + offsetof(GLBChunk, data);
		if(!json.parse(jsonChunk.data, jsonChunk.length)) {
			error("Scene::loadScene: JSON chunk from scene file '{}' could not be parsed.\n", path.string());
			return false;
		}
		char* offset = filebuffer.data() + sizeof(GLBHeader) + offsetof(GLBChunk, data) + jsonChunk.length;
		while(offset - filebuffer.data() < header.length) {
			GLBChunk chunk = *reinterpret_cast<GLBChunk*>(offset);
			assert(chunk.type == GLBChunkType::BIN);
			chunk.data = offset + offsetof(GLBChunk, data);
			offset += offsetof(GLBChunk, data) + chunk.length;
			buffers.emplace_back().assign(chunk.data, chunk.data + chunk.length);
		}

		std::vector<entt::entity>		 entities;
		std::vector<std::vector<size_t>> entitiesChildren;
		const auto&						 root = json.getRoot(); // FIXME: See loadglTF
		for(const auto& n : root["entities"]) {
			auto entity = _registry.create();
			entities.push_back(entity);
			auto& node = _registry.emplace<NodeComponent>(entity);
			node.name = n["name"].asString();
			node.transform = n["transform"].to<glm::mat4>();
			entitiesChildren.emplace_back();
			for(const auto& c : n["children"])
				entitiesChildren.back().push_back(c.asNumber().asInteger());

			if(n.contains("mesh"))
				_registry.emplace<MeshComponent>(entity, MeshComponent{.index = MeshIndex(n["mesh"].asNumber().asInteger())});
		}

		// Update nodes relationships now that they're all available
		for(size_t entityIndex = 0; entityIndex < entitiesChildren.size(); ++entityIndex) {
			auto& parentNode = _registry.get<NodeComponent>(entities[entityIndex]);
			auto& children = entitiesChildren[entityIndex];
			if(children.size() > 0) {
				parentNode.first = entities[children[0]];
				parentNode.children = children.size();
				for(size_t idx = 0; idx < entitiesChildren[entityIndex].size(); ++idx) {
					auto& childNode = _registry.get<NodeComponent>(entities[children[idx]]);
					childNode.parent = entities[entityIndex];
					if(idx > 0)
						childNode.prev = entities[children[idx - 1]];
					if(idx < children.size() - 1)
						childNode.prev = entities[children[idx + 2]];
				}
			}
		}

		for(const auto& m : root["materials"]) {
			loadMaterial(m, 0);
		}

		for(const auto& t : root["textures"]) {
			Textures.push_back(Texture{
				.source = path.parent_path() / t["source"].asString(),
				.format = static_cast<VkFormat>(t["format"].as<int>()),
				.samplerDescription = t["sampler"].asObject(),
			});
		}

		for(const auto& m : root["meshes"]) {
			auto& mesh = _meshes.emplace_back();
			mesh.name = m["name"].asString();
			for(const auto& sm : m["submeshes"]) {
				auto& submesh = mesh.SubMeshes.emplace_back();
				submesh.name = sm["name"].asString();
				submesh.materialIndex = MaterialIndex{static_cast<uint32_t>(sm("material", 0))};
				auto vertexArrayIndex = sm["vertexArray"].as<int>() - 1; // Skipping the JSON chunk
				submesh.getVertices().assign(reinterpret_cast<Vertex*>(buffers[vertexArrayIndex].data()),
											 reinterpret_cast<Vertex*>(buffers[vertexArrayIndex].data() + buffers[vertexArrayIndex].size()));
				auto indexArrayIndex = sm["indexArray"].as<int>() - 1;
				submesh.getIndices().assign(reinterpret_cast<uint32_t*>(buffers[indexArrayIndex].data()),
											reinterpret_cast<uint32_t*>(buffers[indexArrayIndex].data() + buffers[indexArrayIndex].size()));
			}
		}

		computeBounds();
	}
	return true;
}

void Scene::allocateMeshes(const Device& device) {
	if(VertexBuffer) {
		VertexBuffer.destroy();
		IndexBuffer.destroy();
		OffsetTableBuffer.destroy();
		VertexMemory.free();
		IndexMemory.free();
		OffsetTableMemory.free();

		NextVertexMemoryOffset = 0;
		NextIndexMemoryOffset = 0;
	}

	size_t							  totalVertexSize = 0;
	size_t							  totalIndexSize = 0;
	std::vector<VkMemoryRequirements> memReqs;
	for(auto& m : getMeshes()) {
		for(auto& sm : m.SubMeshes) {
			auto vertexBufferMemReq = sm.getVertexBuffer().getMemoryRequirements();
			auto indexBufferMemReq = sm.getIndexBuffer().getMemoryRequirements();
			memReqs.push_back(vertexBufferMemReq);
			memReqs.push_back(indexBufferMemReq);
			sm.indexIntoOffsetTable = _offsetTable.size();
			_offsetTable.push_back(OffsetEntry{static_cast<uint32_t>(sm.materialIndex), static_cast<uint32_t>(totalVertexSize / sizeof(Vertex)),
											   static_cast<uint32_t>(totalIndexSize / sizeof(uint32_t))});
			totalVertexSize += vertexBufferMemReq.size;
			totalIndexSize += indexBufferMemReq.size;
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

	OffsetTableSize = static_cast<uint32_t>(sizeof(OffsetEntry) * _offsetTable.size());
	OffsetTableBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, OffsetTableSize);
	OffsetTableMemory.allocate(device, OffsetTableBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	uploadMeshOffsetTable(device);
}

void Scene::updateMeshOffsetTable() {
	size_t totalVertexSize = 0;
	size_t totalIndexSize = 0;
	_offsetTable.clear();
	for(auto& m : getMeshes()) {
		for(auto& sm : m.SubMeshes) {
			auto vertexBufferMemReq = sm.getVertexBuffer().getMemoryRequirements();
			auto indexBufferMemReq = sm.getIndexBuffer().getMemoryRequirements();
			sm.indexIntoOffsetTable = _offsetTable.size();
			_offsetTable.push_back(OffsetEntry{static_cast<uint32_t>(sm.materialIndex), static_cast<uint32_t>(totalVertexSize / sizeof(Vertex)),
											   static_cast<uint32_t>(totalIndexSize / sizeof(uint32_t))});
			totalVertexSize += vertexBufferMemReq.size;
			totalIndexSize += indexBufferMemReq.size;
		}
	}
}

void Scene::uploadMeshOffsetTable(const Device& device) {
	// Upload OffsetTable via a staging buffer.
	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, OffsetTableSize);
	stagingMemory.allocate(device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	stagingMemory.fill(_offsetTable.data(), _offsetTable.size());

	CommandPool tmpCmdPool;
	tmpCmdPool.create(device, device.getPhysicalDevice().getTransfertQueueFamilyIndex());
	CommandBuffers stagingCommands;
	stagingCommands.allocate(device, tmpCmdPool, 1);
	stagingCommands[0].begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VkBufferCopy copyRegion{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = OffsetTableSize,
	};
	vkCmdCopyBuffer(stagingCommands[0], stagingBuffer, OffsetTableBuffer, 1, &copyRegion);

	stagingCommands[0].end();
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = stagingCommands.getBuffersHandles().data(),
	};
	auto queue = device.getQueue(device.getPhysicalDevice().getTransfertQueueFamilyIndex(), 0);
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(queue));
}

void Scene::onDestroyNodeComponent(entt::registry& registry, entt::entity entity) {
	auto& node = registry.get<NodeComponent>(entity);
	print("Scene::onDestroyNodeComponent '{}' ({})\n", node.name, entity);
	auto child = node.first;
	while(child != entt::null) {
		auto tmp = registry.get<NodeComponent>(child).next;
		registry.destroy(child); // Mmmh...?
		child = tmp;
	}

	if(node.parent != entt::null) {
		auto& parent = registry.get<NodeComponent>(node.parent);
		if(parent.first == entity)
			parent.first = node.next;
		--parent.children;
	}
	if(node.prev != entt::null) {
		auto& prev = registry.get<NodeComponent>(node.prev);
		prev.next = node.next;
	}
	if(node.next != entt::null) {
		auto& next = registry.get<NodeComponent>(node.next);
		next.prev = node.prev;
	}
}

bool Scene::update(const Device& device) {
	if(_dirtyNodes.empty())
		return false;

	// TODO: BLAS
	updateTLAS(device);
	computeBounds();
	_dirtyNodes.clear();
	return true;
}

void Scene::destroyAccelerationStructure(const Device& device) {
	destroyTLAS(device);

	for(const auto& blas : _bottomLevelAccelerationStructures)
		vkDestroyAccelerationStructureKHR(device, blas, nullptr);
	_staticBLASBuffer.destroy();
	_staticBLASMemory.free();
	_bottomLevelAccelerationStructures.clear();
}

void Scene::destroyTLAS(const Device& device) {
	vkDestroyAccelerationStructureKHR(device, _topLevelAccelerationStructure, nullptr);
	_topLevelAccelerationStructure = VK_NULL_HANDLE;
	_tlasBuffer.destroy();
	_tlasMemory.free();
	_accStructInstances.clear();
	_accStructInstancesBuffer.destroy();
	_accStructInstancesMemory.free();
	_tlasScratchBuffer.destroy();
	_tlasScratchMemory.free();
}

void Scene::free(const Device& device) {
	destroyAccelerationStructure(device);

	for(auto& m : getMeshes())
		m.destroy();

	OffsetTableBuffer.destroy();
	IndexBuffer.destroy();
	VertexBuffer.destroy();
	OffsetTableMemory.free();
	VertexMemory.free();
	IndexMemory.free();
}

void Scene::createAccelerationStructure(const Device& device) {
	if(_topLevelAccelerationStructure) {
		destroyAccelerationStructure(device);
	}

	VkFormatProperties2 formatProperties{
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	vkGetPhysicalDeviceFormatProperties2(device.getPhysicalDevice(), VK_FORMAT_R32G32B32_SFLOAT, &formatProperties);
	assert(formatProperties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR);

	VkTransformMatrixKHR rootTransformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

	size_t submeshesCount = 0;
	for(const auto& m : getMeshes())
		submeshesCount += m.SubMeshes.size();
	submeshesCount *= 2; // FIXME

	std::vector<VkAccelerationStructureGeometryKHR>			 geometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 rangeInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*>	 pRangeInfos;
	std::vector<size_t>										 scratchBufferSizes;
	size_t													 scratchBufferSize = 0;
	std::vector<uint32_t>									 blasOffsets; // Start of each BLAS in buffer (aligned to 256 bytes)
	size_t													 totalBLASSize = 0;
	std::vector<VkAccelerationStructureBuildSizesInfoKHR>	 buildSizesInfo;
	geometries.reserve(submeshesCount); // Avoid reallocation since buildInfos will refer to this.
	rangeInfos.reserve(submeshesCount); // Avoid reallocation since pRangeInfos will refer to this.
	blasOffsets.reserve(submeshesCount);
	buildSizesInfo.reserve(submeshesCount);

	const auto& meshes = getMeshes();
	_submeshesIndicesIntoBLASArray.clear();

	{
		QuickTimer qt("BLAS building");
		// Collect all submeshes and query the memory requirements
		for(const auto& mesh : meshes) {
			auto& indices = _submeshesIndicesIntoBLASArray.emplace_back();
			/*
			 * Right now there's a one-to-one relation between submeshes and geometries.
			 * This is not garanteed to be optimal (Apparently less BLAS is better, i.e. grouping geometries), but we don't have a mechanism to
			 * retrieve data for distinct geometries (vertices/indices/material) in our ray tracing shaders yet.
			 * This should be doable using the gl_GeometryIndexEXT built-in.
			 */
			for(size_t i = 0; i < mesh.SubMeshes.size(); ++i) {
				indices.push_back(geometries.size());
				geometries.push_back({
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
					.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
					.geometry =
						VkAccelerationStructureGeometryDataKHR{
							.triangles =
								VkAccelerationStructureGeometryTrianglesDataKHR{
									.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
									.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
									.vertexData = mesh.SubMeshes[i].getVertexBuffer().getDeviceAddress(),
									.vertexStride = sizeof(Vertex),
									.maxVertex = static_cast<uint32_t>(mesh.SubMeshes[i].getVertices().size()),
									.indexType = VK_INDEX_TYPE_UINT32,
									.indexData = mesh.SubMeshes[i].getIndexBuffer().getDeviceAddress(),
									.transformData = 0,
								},
						},
					.flags = 0,
				});

				VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
					.pNext = VK_NULL_HANDLE,
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
					.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
					.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
					.srcAccelerationStructure = VK_NULL_HANDLE,
					.geometryCount = 1,
					.pGeometries = &geometries.back(),
					.ppGeometries = nullptr,
				};

				const uint32_t primitiveCount = static_cast<uint32_t>(mesh.SubMeshes[i].getIndices().size() / 3);

				VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
				vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
														&accelerationStructureBuildSizesInfo);
				buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);

				uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
				blasOffsets.push_back(alignedSize);
				totalBLASSize += alignedSize;

				scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

				buildInfos.push_back(accelerationBuildGeometryInfo);

				rangeInfos.push_back({
					.primitiveCount = primitiveCount,
					.primitiveOffset = 0,
					.firstVertex = 0,
					.transformOffset = 0,
				});
			}
		}

		_staticBLASBuffer.create(device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, totalBLASSize);
		_staticBLASMemory.allocate(device, _staticBLASBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		size_t runningOffset = 0;
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			// Create the acceleration structure
			VkAccelerationStructureKHR			 blas;
			VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
				.pNext = VK_NULL_HANDLE,
				.buffer = _staticBLASBuffer,
				.offset = runningOffset,
				.size = buildSizesInfo[i].accelerationStructureSize,
				.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			};
			VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &blas));
			_bottomLevelAccelerationStructures.push_back(blas);

			buildInfos[i].dstAccelerationStructure = blas;
			runningOffset += blasOffsets[i];
		}

		Buffer		 scratchBuffer; // Temporary buffer used for Acceleration Creation, big enough for all AC so they can be build in parallel
		DeviceMemory scratchMemory;
		scratchBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, scratchBufferSize);
		scratchMemory.allocate(device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
		size_t	   offset = 0;
		const auto scratchBufferAddr = scratchBuffer.getDeviceAddress();
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			buildInfos[i].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			offset += buildSizesInfo[i].buildScratchSize;
			assert(buildInfos[i].geometryCount == 1); // See below! (pRangeInfos will be wrong in this case)
		}
		for(auto& rangeInfo : rangeInfos)
			pRangeInfos.push_back(&rangeInfo); // FIXME: Only work because geometryCount is always 1 here.

		// Build all the bottom acceleration structure on the device via a one-time command buffer submission
		device.immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
			// Build all BLAS in a single call. Note: This might cause sync. issues if buffers are shared (We made sure the scratchBuffer is not.)
			vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(buildInfos.size()), buildInfos.data(), pRangeInfos.data());
		});
	}

	createTLAS(device);
}

void Scene::createTLAS(const Device& device) {
	QuickTimer qt("TLAS building");

	std::vector<uint32_t>			  submeshesIndices;
	std::vector<VkTransformMatrixKHR> transforms;
	const auto&						  meshes = getMeshes();
	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		if(auto* mesh = _registry.try_get<MeshComponent>(entity); mesh != nullptr) {
			transform = glm::transpose(transform); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			for(size_t i = 0; i < meshes[mesh->index].SubMeshes.size(); ++i) {
				// Get the bottom acceleration structures' handle, which will be used during the top level acceleration build
				VkAccelerationStructureDeviceAddressInfoKHR BLASAddressInfo{
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
					.accelerationStructure = _bottomLevelAccelerationStructures[_submeshesIndicesIntoBLASArray[mesh->index][i]],
				};
				auto BLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &BLASAddressInfo);

				submeshesIndices.push_back(meshes[mesh->index].SubMeshes[i].indexIntoOffsetTable);
				transforms.push_back(*reinterpret_cast<VkTransformMatrixKHR*>(&transform));
				_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
					.transform = *reinterpret_cast<VkTransformMatrixKHR*>(&transform),
					.instanceCustomIndex = meshes[mesh->index].SubMeshes[i].indexIntoOffsetTable,
					.mask = 0xFF,
					.instanceShaderBindingTableRecordOffset = 0,
					.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
					.accelerationStructureReference = BLASDeviceAddress,
				});
			}
		}
	});
	_accStructInstancesBuffer.create(device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
									 _accStructInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
	_accStructInstancesMemory.allocate(device, _accStructInstancesBuffer,
									   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	_accStructInstancesMemory.fill(_accStructInstances.data(), _accStructInstances.size());

	VkAccelerationStructureGeometryKHR TLASGeometry{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry =
			{
				.instances =
					{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
						.arrayOfPointers = VK_FALSE,
						.data = _accStructInstancesBuffer.getDeviceAddress(),
					},
			},
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
	};

	const uint32_t							 TBLAPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TLASBuildGeometryInfo, &TBLAPrimitiveCount, &TLASBuildSizesInfo);

	_tlasBuffer.create(device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, TLASBuildSizesInfo.accelerationStructureSize);
	_tlasMemory.allocate(device, _tlasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = _tlasBuffer,
		.size = TLASBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};

	VK_CHECK(vkCreateAccelerationStructureKHR(device, &TLASCreateInfo, nullptr, &_topLevelAccelerationStructure));

	_tlasScratchBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, TLASBuildSizesInfo.buildScratchSize);
	_tlasScratchMemory.allocate(device, _tlasScratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	TLASBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	TLASBuildGeometryInfo.dstAccelerationStructure = _topLevelAccelerationStructure;
	TLASBuildGeometryInfo.scratchData = {.deviceAddress = _tlasScratchBuffer.getDeviceAddress()};

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TBLAPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	device.immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

void Scene::updateTLAS(const Device& device) {
	// TODO: Optimise (including with regards to the GPU sync.).
	// FIXME: Re-traversing the entire hierarchy to update the transforms could be avoided (especially since modified nodes are marked).
	QuickTimer qt("TLAS update");

	uint32_t	index = 0;
	const auto& meshes = getMeshes();
	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		if(auto* mesh = _registry.try_get<MeshComponent>(entity); mesh != nullptr) {
			transform = glm::transpose(transform); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			for(size_t i = 0; i < meshes[mesh->index].SubMeshes.size(); ++i) {
				_accStructInstances[index].transform = *reinterpret_cast<VkTransformMatrixKHR*>(&transform);
				++index;
			}
		}
	});
	_accStructInstancesMemory.fill(_accStructInstances.data(), _accStructInstances.size());

	VkAccelerationStructureGeometryKHR TLASGeometry{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry =
			{
				.instances =
					{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
						.arrayOfPointers = VK_FALSE,
						.data = _accStructInstancesBuffer.getDeviceAddress(),
					},
			},
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
	};
	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
		.srcAccelerationStructure = _topLevelAccelerationStructure,
		.dstAccelerationStructure = _topLevelAccelerationStructure,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
		.scratchData = {.deviceAddress = _tlasScratchBuffer.getDeviceAddress()},
	};
	const uint32_t							 TBLAPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TBLAPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	device.immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

entt::entity Scene::intersectNodes(Ray& ray) {
	Hit			 best;
	entt::entity bestNode = entt::null;
	const auto&	 meshes = getMeshes();
	// Note: If we had cached/precomputed node bounds (without the need of meshes), we could speed this up a lot (basically an acceleration structure).
	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		if(auto* mesh = _registry.try_get<MeshComponent>(entity); mesh != nullptr) {
			auto hit = intersect(ray, transform * meshes[mesh->index].getBounds());
			if(hit.hit && hit.depth < best.depth) {
				Ray localRay = glm::inverse(transform) * ray;
				hit = intersect(localRay, meshes[mesh->index]);
				hit.depth = glm::length(glm::vec3(transform * glm::vec4((localRay.origin + localRay.direction * hit.depth), 1.0f)) - ray.origin);
				if(hit.hit && hit.depth < best.depth) {
					best = hit;
					bestNode = entity;
				}
			}
		}
	});
	return bestNode;
}
