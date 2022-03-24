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

static glm::mat4 getGrobalTransform(const entt::registry& registry, const NodeComponent& node) {
	auto transform = node.transform;
	auto parent = node.parent;
	while(parent != entt::null) {
		const auto& parentNode = registry.get<NodeComponent>(parent);
		transform = parentNode.transform * transform;
		parent = parentNode.parent;
	}
	return transform;
}

Scene::Scene() {
	_registry.on_destroy<NodeComponent>().connect<&Scene::onDestroyNodeComponent>(this);
	_root = _registry.create();
	_registry.emplace<NodeComponent>(_root).name = "Root";
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
		if(!json.parse(path)) {
			error("Scene::loadglTF error: Could not parse '{}'.\n", path.string());
			return false;
		}
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

	const auto textureOffset = static_cast<uint32_t>(Textures.size());

	loadTextures(path, object);

	const auto materialOffset = Materials.size();

	if(object.contains("materials"))
		for(const auto& mat : object["materials"])
			loadMaterial(mat, textureOffset);

	const MeshIndex meshOffset{static_cast<MeshIndex::UnderlyingType>(_meshes.size())};

	std::vector<std::vector<MeshIndex>> gltfIndexToMeshIndices;

	for(const auto& m : object["meshes"]) {
		auto baseName = m("name", std::string("UnamedMesh"));
		gltfIndexToMeshIndices.emplace_back();
		for(const auto& p : m["primitives"]) {
			gltfIndexToMeshIndices.back().push_back(MeshIndex(_meshes.size()));
			auto& mesh = _meshes.emplace_back();
			mesh.name = baseName + "_" + p("name", std::string("Unamed"));
			if(p.asObject().contains("material")) {
				mesh.defaultMaterialIndex.value = materialOffset + p["material"].as<int>();
			}

			if(p.contains("mode"))
				assert(p["mode"].as<int>() == 4); // We only supports triangles
			const auto& positionAccessor = object["accessors"][p["attributes"]["POSITION"].as<int>()];
			const auto& positionBufferView = object["bufferViews"][positionAccessor["bufferView"].as<int>()];
			const auto& positionBuffer = buffers[positionBufferView["buffer"].as<int>()];

			const auto& normalAccessor = object["accessors"][p["attributes"]["NORMAL"].as<int>()];
			const auto& normalBufferView = object["bufferViews"][normalAccessor["bufferView"].as<int>()];
			const auto& normalBuffer = buffers[normalBufferView["buffer"].as<int>()];

			auto initAccessor = [&](const std::string& name, const char** bufferData, size_t* cursor, size_t* stride, const int defaultStride = 4 * sizeof(float),
									ComponentType expectedComponentType = ComponentType::Float, const std::string& expectedType = "VEC4") {
				const auto& accessor = object["accessors"][p["attributes"][name].as<int>()];
				assert(static_cast<ComponentType>(accessor["componentType"].as<int>()) == expectedComponentType);
				assert(accessor["type"].as<std::string>() == expectedType);
				const auto& bufferView = object["bufferViews"][accessor["bufferView"].as<int>()];
				const auto& buffer = buffers[bufferView["buffer"].as<int>()];
				*bufferData = buffer.data();
				*cursor = accessor("byteOffset", 0) + bufferView("byteOffset", 0);
				*stride = bufferView("byteStride", defaultStride);
			};

			const char* tangentBufferData = nullptr;
			size_t		tangentCursor = 0;
			size_t		tangentStride = 0;
			if(p["attributes"].contains("TANGENT")) {
				initAccessor("TANGENT", &tangentBufferData, &tangentCursor, &tangentStride);
			}
			// TODO: Compute tangents if not present in file.

			const char* texCoordBufferData = nullptr;
			size_t		texCoordCursor = 0;
			size_t		texCoordStride = 0;
			if(p["attributes"].contains("TEXCOORD_0")) {
				initAccessor("TEXCOORD_0", &texCoordBufferData, &texCoordCursor, &texCoordStride, 2 * sizeof(float), ComponentType::Float, "VEC2");
			}

			bool		skinnedMesh = p["attributes"].contains("WEIGHTS_0");
			const char* weightsBufferData = nullptr;
			size_t		weightsCursor = 0;
			size_t		weightsStride = 0;
			const char* jointsBufferData = nullptr;
			size_t		jointsCursor = 0;
			size_t		jointsStride = 0;

			if(skinnedMesh) {
				initAccessor("WEIGHTS_0", &weightsBufferData, &weightsCursor, &weightsStride);
				assert(p["attributes"].contains("JOINTS_0"));
				initAccessor("JOINTS_0", &jointsBufferData, &jointsCursor, &jointsStride, 4 * sizeof(JointIndex), ComponentType::UnsignedShort);
			}
			std::vector<glm::vec4>	  weights;
			std::vector<JointIndices> joints;

			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].as<int>()) == ComponentType::Float); // TODO
				assert(static_cast<ComponentType>(normalAccessor["componentType"].as<int>()) == ComponentType::Float);	 // TODO
				size_t positionCursor = positionAccessor("byteOffset", 0) + positionBufferView("byteOffset", 0);
				size_t positionStride = positionBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				size_t normalCursor = normalAccessor("byteOffset", 0) + normalBufferView("byteOffset", 0);
				size_t normalStride = normalBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				assert(positionAccessor["count"].as<int>() == normalAccessor["count"].as<int>());
				mesh.getVertices().reserve(positionAccessor["count"].as<int>());
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

					if(skinnedMesh) {
						weights.push_back(*reinterpret_cast<const glm::vec4*>(weightsBufferData + weightsCursor));
						weightsCursor += weightsStride;
						joints.push_back(*reinterpret_cast<const JointIndices*>(jointsBufferData + jointsCursor));
						jointsCursor += jointsStride;
					}

					mesh.getVertices().push_back(v);
				}
			} else {
				error("Error: Unsupported accessor type '{}'.", positionAccessor["type"].asString());
			}

			// TODO: Use the Mesh's SKIN!
			if(skinnedMesh)
				mesh.setSkin(SkinVertexData{weights, joints});

			if(positionAccessor.contains("min") && positionAccessor.contains("max")) {
				mesh.setBounds({
					.min = positionAccessor["min"].to<glm::vec3>(),
					.max = positionAccessor["max"].to<glm::vec3>(),
				});
			} else {
				mesh.computeBounds();
			}
			if(!mesh.getBounds().isValid())
				mesh.computeBounds();
			assert(mesh.getBounds().isValid());

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
				mesh.getIndices().reserve(indicesAccessor["count"].as<int>());
				for(size_t i = 0; i < indicesAccessor["count"].as<int>(); ++i) {
					uint32_t idx = 0;
					switch(compType) {
						case ComponentType::UnsignedShort: idx = *reinterpret_cast<const unsigned short*>(indicesBuffer.data() + cursor); break;
						case ComponentType::UnsignedInt: idx = *reinterpret_cast<const unsigned int*>(indicesBuffer.data() + cursor); break;
						default: assert(false);
					}
					mesh.getIndices().push_back(idx);
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
		if(meshIndex != -1) {
			const auto& indices = gltfIndexToMeshIndices[meshIndex];
			auto		addRenderer = [&](entt::entity entity, MeshIndex meshIndex) {
				   if(getMeshes()[meshIndex].isSkinned()) {
					   assert(node.contains("skin"));
					   _registry.emplace<SkinnedMeshRendererComponent>(entity, SkinnedMeshRendererComponent{
																					   .meshIndex = MeshIndex(meshIndex),
																					   .materialIndex = getMeshes()[meshIndex].defaultMaterialIndex,
																					   .skinIndex = node["skin"].as<int>() + _skins.size(),
																			   });
				   } else
					   _registry.emplace<MeshRendererComponent>(entity, MeshRendererComponent{
																				.meshIndex = MeshIndex(meshIndex),
																				.materialIndex = getMeshes()[meshIndex].defaultMaterialIndex,
																		});
			};
			if(indices.size() == 0)
				warn("Mesh {} has no primitives.\n", meshIndex);
			else if(indices.size() == 1)
				addRenderer(entity, MeshIndex{indices[0]});
			else {
				for(const auto& idx : indices) {
					auto  submesh = _registry.create();
					auto& submeshNode = _registry.emplace<NodeComponent>(submesh);
					addChild(entity, submesh);
					addRenderer(submesh, MeshIndex{idx});
				}
			}
		}
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

	if(object.contains("skins"))
		for(const auto& skin : object["skins"]) {
			const auto& accessor = object["accessors"][skin["inverseBindMatrices"].as<int>()];
			assert(static_cast<ComponentType>(accessor["componentType"].as<int>()) == ComponentType::Float);
			assert(accessor["type"].as<std::string>() == "MAT4");
			const auto&			   bufferView = object["bufferViews"][accessor["bufferView"].as<int>()];
			auto				   count = accessor["count"].as<int>();
			const auto&			   buffer = buffers[bufferView["buffer"].as<int>()];
			auto				   bufferData = buffer.data();
			auto				   cursor = accessor("byteOffset", 0) + bufferView("byteOffset", 0);
			int					   defaultStride = sizeof(glm::mat4);
			auto				   stride = bufferView("byteStride", defaultStride);
			std::vector<glm::mat4> inverseBindMatrices;
			for(int i = 0; i < count; ++i) {
				inverseBindMatrices.push_back(*reinterpret_cast<const glm::mat4*>(bufferData + cursor));
				cursor += stride;
			}
			std::vector<entt::entity> joints;
			for(const auto& nodeIndex : skin["joints"])
				joints.push_back(entities[nodeIndex.as<int>()]);
			_skins.push_back({inverseBindMatrices, joints});
		}

	if(object.contains("animations"))
		for(const auto& anim : object["animations"]) {}

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

	entt::entity meshEntity = entt::null;
	entt::entity sm = entt::null;
	Mesh*		 m = nullptr;

	auto  rootEntity = _registry.create();
	auto& n = _registry.emplace<NodeComponent>(rootEntity);
	addChild(_root, rootEntity);

	std::vector<glm::vec2> uvs;
	std::vector<glm::vec3> normals;
	uint32_t			   vertexOffset = 0;

	const auto nextMesh = [&]() {
		meshEntity = _registry.create();
		_registry.emplace<NodeComponent>(meshEntity);
		addChild(rootEntity, meshEntity);
	};
	const auto nextSubMesh = [&]() {
		if(m)
			vertexOffset += static_cast<uint32_t>(m->getVertices().size());
		m = &_meshes.emplace_back();
		auto submeshEntity = _registry.create();
		_registry.emplace<NodeComponent>(submeshEntity);
		addChild(meshEntity, submeshEntity);
		_registry.emplace<MeshRendererComponent>(submeshEntity, MeshIndex{static_cast<MeshIndex>(_meshes.size() - 1)});
	};

	nextMesh();
	nextSubMesh();

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
				m->getVertices().push_back(v);
			}
		} else if(line[0] == 'f') {
			char* cur = line.data() + 2;
			for(size_t i = 0; i < 3; ++i) { // Supports only triangles.
				auto vertexIndex = static_cast<uint32_t>(std::strtol(cur, &cur, 10));
				assert(vertexIndex > 0 && vertexIndex != std::numeric_limits<long int>::max());
				vertexIndex -= 1;			 // Indices starts at 1 in .obj
				vertexIndex -= vertexOffset; // Indices are absolutes in the obj file, we're relative to the current submesh
				m->getIndices().push_back(vertexIndex);
				// Assuming vertices, uvs and normals have already been defined.
				if(*cur == '/') {
					++cur;
					// Texture coordinate index
					auto uvIndex = std::strtol(cur, &cur, 10);
					if(uvIndex > 0 && uvIndex != std::numeric_limits<long int>::max())
						m->getVertices()[vertexIndex].texCoord = uvs[uvIndex - 1];
					// Normal index
					if(*cur == '/') {
						++cur;
						auto normalIndex = std::strtol(cur, &cur, 10);
						if(normalIndex > 0 && normalIndex != std::numeric_limits<long int>::max())
							m->getVertices()[vertexIndex].normal = normals[normalIndex - 1];
					}
				}
			}
		} else if(line[0] == 'o') {
			// Next Mesh
			if(m->getVertices().size() > 0)
				nextMesh();
		} else if(line[0] == 'g') {
			// Next SubMesh
			if(m->getVertices().size() > 0) {
				m->computeVertexNormals();
				m->computeBounds();
				nextSubMesh();
			}
		} else {
			warn("Unsupported OBJ command: '{}' (Full line: '{}')\n", line[0], line);
		}
	}
	// FIXME: Something's broken (see Raytracing debug view: Almost all black)
	m->computeVertexNormals();
	m->computeBounds();
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
		  {"entities", JSON::array()},
		  {"meshes", JSON::array()},
	  };
	auto& root = serialized.getRoot();

	auto& mats = root["materials"].asArray();
	for(const auto& mat : Materials)
		mats.push_back(toJSON(mat));

	auto&									 entities = root["entities"].asArray();
	auto									 view = _registry.view<NodeComponent>();
	std::unordered_map<entt::entity, size_t> entitiesIndices;
	for(const auto& entity : view) {
		auto& n = _registry.get<NodeComponent>(entity);
		JSON  nodeJSON{
			 {"name", n.name},
			 {"transform", toJSON(n.transform)},
			 {"parent", -1},
			 {"children", JSON::array()},
		 };
		if(auto* mesh = _registry.try_get<MeshRendererComponent>(entity); mesh != nullptr) {
			nodeJSON["meshRenderer"] = JSON{
				{"meshIndex", static_cast<int>(mesh->meshIndex)},
				{"materialIndex", static_cast<int>(mesh->materialIndex)},
			};
		}
		entitiesIndices[entity] = entities.size(); // Maps entity id to index in the file array, used to create children array
		entities.push_back(nodeJSON);
	}
	// Add childrens
	size_t index = 0;
	for(const auto& entity : view) {
		auto& n = _registry.get<NodeComponent>(entity);
		for(auto c = n.first; c != entt::null; c = _registry.get<NodeComponent>(c).next)
			entities[index]["children"].push(entitiesIndices[c]);
		++index;
	}

	std::vector<GLBChunk> buffers;
	buffers.push_back({
		.type = GLBChunkType::JSON,
	}); // This will become the main JSON chunk header
	auto& meshes = root["meshes"].asArray();
	for(const auto& m : _meshes) {
		int	 offset = static_cast<int>(buffers.size());
		JSON mesh{
			{"name", m.name},
			{"material", m.defaultMaterialIndex.value},
			{"vertexArray", offset + 0},
			{"indexArray", offset + 1},
		};
		buffers.push_back(GLBChunk{static_cast<uint32_t>(m.getVertexByteSize()), GLBChunkType::BIN, reinterpret_cast<char*>(const_cast<Vertex*>(m.getVertices().data()))});
		buffers.push_back(GLBChunk{static_cast<uint32_t>(m.getIndexByteSize()), GLBChunkType::BIN, reinterpret_cast<char*>(const_cast<uint32_t*>(m.getIndices().data()))});
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
	_registry.clear();
	_root = entt::null;
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

			if(n.contains("meshRenderer")) {
				_registry.emplace<MeshRendererComponent>(entity, MeshRendererComponent{
																	 .meshIndex = MeshIndex(n["meshRenderer"]["meshIndex"].asNumber().asInteger()),
																	 .materialIndex = MaterialIndex(n["meshRenderer"]["materialIndex"].asNumber().asInteger()),
																 });
			}
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
						childNode.next = entities[children[idx + 1]];
				}
			}
		}

		for(const auto& t : root["textures"]) {
			Textures.push_back(Texture{
				.source = path.parent_path() / t["source"].asString(),
				.format = static_cast<VkFormat>(t["format"].as<int>()),
				.samplerDescription = t["sampler"].asObject(),
			});
		}

		for(const auto& m : root["materials"]) {
			loadMaterial(m, 0);
		}

		for(const auto& m : root["meshes"]) {
			auto& mesh = _meshes.emplace_back();
			mesh.name = m["name"].asString();
			mesh.defaultMaterialIndex = MaterialIndex{static_cast<uint32_t>(m("material", 0))};
			auto vertexArrayIndex = m["vertexArray"].as<int>() - 1; // Skipping the JSON chunk
			mesh.getVertices().assign(reinterpret_cast<Vertex*>(buffers[vertexArrayIndex].data()),
									  reinterpret_cast<Vertex*>(buffers[vertexArrayIndex].data() + buffers[vertexArrayIndex].size()));
			auto indexArrayIndex = m["indexArray"].as<int>() - 1;
			mesh.getIndices().assign(reinterpret_cast<uint32_t*>(buffers[indexArrayIndex].data()),
									 reinterpret_cast<uint32_t*>(buffers[indexArrayIndex].data() + buffers[indexArrayIndex].size()));
		}

		// Find root (FIXME: There's probably a better way to do this. Should we order the nodes when saving so the root is always the first node in the array? It's also probably a
		// win for performance, mmh...)
		for(auto e : entities)
			if(_registry.get<NodeComponent>(e).parent == entt::null) { // Note: This means that we don't support out-of-tree entities, but it's probably fine.
				_root = e;
				break;
			}
		computeBounds();
		return true;
	}
	return false;
}

void Scene::allocateMeshes(const Device& device) {
	if(VertexBuffer) {
		VertexBuffer.destroy();
		IndexBuffer.destroy();
		OffsetTableBuffer.destroy();
		VertexMemory.free();
		IndexMemory.free();
		OffsetTableMemory.free();

		NextVertexMemoryOffsetInBytes = 0;
		NextIndexMemoryOffsetInBytes = 0;
	}

	updateMeshOffsetTable();
	auto vertexMemoryTypeBits = getMeshes()[0].getVertexBuffer().getMemoryRequirements().memoryTypeBits;
	auto indexMemoryTypeBits = getMeshes()[0].getIndexBuffer().getMemoryRequirements().memoryTypeBits;
	VertexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(vertexMemoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
						  StaticVertexBufferSizeInBytes + MaxDynamicVertexSizeInBytes); // Allocate more memory for dynamic (skinned) meshes.
	IndexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(indexMemoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), StaticIndexBufferSizeInBytes);
	for(auto meshIdx = 0; meshIdx < getMeshes().size(); ++meshIdx) {
		vkBindBufferMemory(device, getMeshes()[meshIdx].getVertexBuffer(), VertexMemory, NextVertexMemoryOffsetInBytes);
		NextVertexMemoryOffsetInBytes += getMeshes()[meshIdx].getVertexBuffer().getMemoryRequirements().size;
		vkBindBufferMemory(device, getMeshes()[meshIdx].getIndexBuffer(), IndexMemory, NextIndexMemoryOffsetInBytes);
		NextIndexMemoryOffsetInBytes += getMeshes()[meshIdx].getIndexBuffer().getMemoryRequirements().size;
	}
	// Create views to the entire dataset
	VertexBuffer.create(device,
						VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						StaticVertexBufferSizeInBytes + MaxDynamicVertexSizeInBytes);
	vkBindBufferMemory(device, VertexBuffer, VertexMemory, 0);
	IndexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, StaticIndexBufferSizeInBytes);
	vkBindBufferMemory(device, IndexBuffer, IndexMemory, 0);

	OffsetTableBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							 StaticOffsetTableSizeInBytes + 1024 * sizeof(OffsetEntry)); // FIXME: Static memory for 1024 additional dynamic instances
	OffsetTableMemory.allocate(device, OffsetTableBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	uploadMeshOffsetTable(device);

	allocateDynamicMeshes(device);
}

void Scene::updateMeshOffsetTable() {
	size_t totalVertexSize = 0;
	size_t totalIndexSize = 0;
	_offsetTable.clear();
	for(auto& m : getMeshes()) {
		auto vertexBufferMemReq = m.getVertexBuffer().getMemoryRequirements();
		auto indexBufferMemReq = m.getIndexBuffer().getMemoryRequirements();
		m.indexIntoOffsetTable = static_cast<uint32_t>(_offsetTable.size());
		_offsetTable.push_back(OffsetEntry{
			static_cast<uint32_t>(m.defaultMaterialIndex),
			static_cast<uint32_t>(totalVertexSize / sizeof(Vertex)),
			static_cast<uint32_t>(totalIndexSize / sizeof(uint32_t)),
		});
		totalVertexSize += vertexBufferMemReq.size;
		totalIndexSize += indexBufferMemReq.size;
	}
	StaticVertexBufferSizeInBytes = totalVertexSize;
	StaticIndexBufferSizeInBytes = totalIndexSize;
	StaticOffsetTableSizeInBytes = static_cast<uint32_t>(sizeof(OffsetEntry) * _offsetTable.size());
}

void Scene::uploadMeshOffsetTable(const Device& device) {
	copyViaStagingBuffer(device, OffsetTableBuffer, _offsetTable);
}

void Scene::allocateDynamicMeshes(const Device& device) {
	sortRenderers();
	updateDynamicMeshOffsetTable();
	uploadDynamicMeshOffsetTable(device);
	_updateQueryPools.resize(2);
	for(size_t i = 0; i < 2; i++) {
		_updateQueryPools[i].create(device, VK_QUERY_TYPE_TIMESTAMP, 2);
	}
}

void Scene::updateDynamicMeshOffsetTable() {
	auto   instances = getRegistry().view<SkinnedMeshRendererComponent>();
	size_t totalVertexSize = 0;
	size_t idx = 0;
	_dynamicOffsetTable.clear();
	for(auto& entity : instances) {
		auto& skinnedMeshRenderer = getRegistry().get<SkinnedMeshRendererComponent>(entity);
		auto  vertexBufferMemReq = _meshes[skinnedMeshRenderer.meshIndex].getVertexBuffer().getMemoryRequirements();
		skinnedMeshRenderer.indexIntoOffsetTable = static_cast<uint32_t>(_offsetTable.size() + _dynamicOffsetTable.size());
		_dynamicOffsetTable.push_back(OffsetEntry{
			static_cast<uint32_t>(skinnedMeshRenderer.materialIndex),
			static_cast<uint32_t>((StaticVertexBufferSizeInBytes + totalVertexSize) / sizeof(Vertex)),
			static_cast<uint32_t>(_offsetTable[_meshes[skinnedMeshRenderer.meshIndex].indexIntoOffsetTable].indexOffset),
		});
		totalVertexSize += vertexBufferMemReq.size;
	}
	assert(totalVertexSize < MaxDynamicVertexSizeInBytes);
	assert(_dynamicOffsetTable.size() < 1024);

	DynamicOffsetTableSizeInBytes = static_cast<uint32_t>(sizeof(OffsetEntry) * _dynamicOffsetTable.size());
}

void Scene::uploadDynamicMeshOffsetTable(const Device& device) {
	if(_dynamicOffsetTable.size() > 0)
		copyViaStagingBuffer(device, OffsetTableBuffer, _dynamicOffsetTable, 0, StaticOffsetTableSizeInBytes);
}

bool Scene::updateDynamicVertexBuffer(const Device& device, float deltaTime) {
	// QuickTimer qt("Update Dynamic Vertex Buffer");
	//  TODO: CPU first, then move it to a compute shader?
	auto instances = getRegistry().view<SkinnedMeshRendererComponent>();
	if(instances.empty())
		return false;
	std::vector<Vertex> transformedVertices;
	for(auto& entity : instances) {
		auto&		skinnedMeshRenderer = getRegistry().get<SkinnedMeshRendererComponent>(entity);
		const auto& skin = _skins[skinnedMeshRenderer.skinIndex];
		/*
		std::vector<glm::mat4> frame;
		if(skinnedMeshRenderer.animationIndex == -1)
			frame = SkeletalAnimation{.jointsCount = static_cast<uint32_t>(skin.joints.size())}.at(0);
		else
			frame = _animations[skinnedMeshRenderer.animationIndex].at(0); // FIXME: Get time
		*/

		// FIXME: Temp testing
		static std::vector<glm::mat4> frame = SkeletalAnimation{.jointsCount = static_cast<uint32_t>(skin.joints.size())}.at(0);
		for(auto& m : frame)
			m = glm::rotate(deltaTime, glm::vec3(0, 1, 0)) * m;

		const auto& mesh = _meshes[skinnedMeshRenderer.meshIndex];
		const auto& vertices = _meshes[skinnedMeshRenderer.meshIndex].getVertices();
		const auto& joints = _meshes[skinnedMeshRenderer.meshIndex].getSkin().joints;
		const auto& weights = _meshes[skinnedMeshRenderer.meshIndex].getSkin().weights;
		for(size_t i = 0; i < vertices.size(); ++i) {
			Vertex v = vertices[i];
			auto   skinMatrix = weights[i][0] * frame[joints[i].indices[0]] + weights[i][1] * frame[joints[i].indices[1]] + weights[i][2] * frame[joints[i].indices[2]] +
							  weights[i][3] * frame[joints[i].indices[3]];
			v.pos = glm::vec3(skinMatrix * glm::vec4(v.pos, 1.0));
			transformedVertices.push_back(v);
		}
	}
	copyViaStagingBuffer(device, VertexBuffer, transformedVertices, 0, StaticVertexBufferSizeInBytes);
	return true;
}

bool Scene::update(const Device& device, float deltaTime) {
	QuickTimer qt(_updateTimes);
	bool	   hierarchicalChanges = false;
	if(!_dirtyNodes.empty()) {
		// FIXME: Re-traversing the entire hierarchy to update the transforms could be avoided (especially since modified nodes are marked).
		updateTransforms(device);
		updateAccelerationStructureInstances(device);
		computeBounds();
		_dirtyNodes.clear();
		hierarchicalChanges = true;
	}

	auto vertexUpdate = updateDynamicVertexBuffer(device, deltaTime);
	auto blasUpdate = vertexUpdate && updateDynamicBLAS(device);

	// TLAS has to be updated if the hierarchy has changed, or some BLAS were updated (like skinned meshes).
	if(hierarchicalChanges || blasUpdate)
		updateTLAS(device);

	return hierarchicalChanges;
}

void Scene::destroyAccelerationStructure(const Device& device) {
	destroyTLAS(device);

	for(const auto& blas : _bottomLevelAccelerationStructures)
		vkDestroyAccelerationStructureKHR(device, blas, nullptr);
	_staticBLASBuffer.destroy();
	_staticBLASMemory.free();
	_bottomLevelAccelerationStructures.clear();
	_blasScratchBuffer.destroy();
	_blasScratchMemory.free();

	_updateQueryPools.clear();
}

void Scene::destroyTLAS(const Device& device) {
	vkDestroyAccelerationStructureKHR(device, _topLevelAccelerationStructure, nullptr);
	_topLevelAccelerationStructure = VK_NULL_HANDLE;
	_tlasBuffer.destroy();
	_tlasMemory.free();
	_instancesBuffer.destroy();
	_instancesMemory.free();
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
	NextVertexMemoryOffsetInBytes = 0;
	NextIndexMemoryOffsetInBytes = 0;
	StaticOffsetTableSizeInBytes = 0;
	StaticVertexBufferSizeInBytes = 0;
}

inline static [[nodiscard]] VkAccelerationStructureBuildSizesInfoKHR getBuildSize(const Device&										device,
																				  const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo,
																				  const uint32_t									primitiveCount) {
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
											&accelerationStructureBuildSizesInfo);
	return accelerationStructureBuildSizesInfo;
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

	size_t meshesCount = getMeshes().size() + MaxDynamicBLAS; // FIXME: Reserve MaxDynamicBLAS dynamic instances

	std::vector<VkAccelerationStructureGeometryKHR>			 geometries;
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR>	 rangeInfos;
	std::vector<size_t>										 scratchBufferSizes;
	size_t													 scratchBufferSize = 0;
	std::vector<uint32_t>									 blasOffsets; // Start of each BLAS in buffer (aligned to 256 bytes)
	size_t													 totalBLASSize = 0;
	std::vector<VkAccelerationStructureBuildSizesInfoKHR>	 buildSizesInfo;
	geometries.reserve(meshesCount); // Avoid reallocation since buildInfos will refer to this.
	rangeInfos.reserve(meshesCount); // Avoid reallocation since pRangeInfos will refer to this.
	blasOffsets.reserve(meshesCount);
	buildSizesInfo.reserve(meshesCount);
	_dynamicBLASGeometries.clear();
	_dynamicBLASGeometries.reserve(MaxDynamicBLAS);
	_dynamicBLASBuildGeometryInfos.clear();
	_dynamicBLASBuildGeometryInfos.reserve(MaxDynamicBLAS);
	_dynamicBLASBuildRangeInfos.clear();
	_dynamicBLASBuildRangeInfos.reserve(MaxDynamicBLAS);

	VkAccelerationStructureGeometryKHR baseGeometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry =
			VkAccelerationStructureGeometryDataKHR{
				.triangles =
					VkAccelerationStructureGeometryTrianglesDataKHR{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexData = 0,
						.vertexStride = sizeof(Vertex),
						.maxVertex = 0,
						.indexType = VK_INDEX_TYPE_UINT32,
						.indexData = 0,
						.transformData = 0,
					},
			},
		.flags = 0,
	};

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.pNext = VK_NULL_HANDLE,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.srcAccelerationStructure = VK_NULL_HANDLE,
		.geometryCount = 1,
		.pGeometries = nullptr,
		.ppGeometries = nullptr,
	};

	const auto& meshes = getMeshes();

	{
		QuickTimer qt("BLAS building");
		// Collect all submeshes and query the memory requirements
		const size_t staticBLASCount = meshes.size();
		for(const auto& mesh : meshes) {
			/*
			 * Right now there's a one-to-one relation between meshes and geometries.
			 * This is not garanteed to be optimal (Apparently less BLAS is better, i.e. grouping geometries), but we don't have a mechanism to
			 * retrieve data for distinct geometries (vertices/indices/material) in our ray tracing shaders yet.
			 * This should be doable using the gl_GeometryIndexEXT built-in.
			 */
			baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{mesh.getVertexBuffer().getDeviceAddress()};
			baseGeometry.geometry.triangles.maxVertex = static_cast<uint32_t>(mesh.getVertices().size());
			baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
			geometries.push_back(baseGeometry);

			accelerationBuildGeometryInfo.pGeometries = &geometries.back();

			const uint32_t primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3);

			auto accelerationStructureBuildSizesInfo = getBuildSize(device, accelerationBuildGeometryInfo, primitiveCount);

			uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
			totalBLASSize += alignedSize;
			scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

			buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);
			blasOffsets.push_back(alignedSize);
			buildInfos.push_back(accelerationBuildGeometryInfo);
			rangeInfos.push_back({
				.primitiveCount = primitiveCount,
				.primitiveOffset = 0,
				.firstVertex = 0,
				.transformOffset = 0,
			});
		}

		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		// Create an additional BLAS for each skinned mesh instance.
		updateDynamicVertexBuffer(device, 0); // Make sure the vertex data is available. FIXME: Should not be there.
		auto instances = getRegistry().view<SkinnedMeshRendererComponent>();
		for(auto& entity : instances) {
			auto& skinnedMeshRenderer = getRegistry().get<SkinnedMeshRendererComponent>(entity);
			auto& mesh = getMeshes()[skinnedMeshRenderer.meshIndex];

			skinnedMeshRenderer.blasIndex = buildInfos.size();

			baseGeometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR{
				VertexBuffer.getDeviceAddress() +
				sizeof(Vertex) * _dynamicOffsetTable[skinnedMeshRenderer.indexIntoOffsetTable - StaticOffsetTableSizeInBytes / sizeof(OffsetEntry)].vertexOffset};
			baseGeometry.geometry.triangles.maxVertex = static_cast<uint32_t>(mesh.getVertices().size());
			baseGeometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR{mesh.getIndexBuffer().getDeviceAddress()};
			geometries.push_back(baseGeometry);

			accelerationBuildGeometryInfo.pGeometries = &geometries.back();

			const uint32_t primitiveCount = static_cast<uint32_t>(mesh.getIndices().size() / 3);

			auto accelerationStructureBuildSizesInfo = getBuildSize(device, accelerationBuildGeometryInfo, primitiveCount);

			uint32_t alignedSize = static_cast<uint32_t>(std::ceil(accelerationStructureBuildSizesInfo.accelerationStructureSize / 256.0)) * 256;
			totalBLASSize += alignedSize;
			scratchBufferSize += accelerationStructureBuildSizesInfo.buildScratchSize;

			buildSizesInfo.push_back(accelerationStructureBuildSizesInfo);
			blasOffsets.push_back(alignedSize);
			buildInfos.push_back(accelerationBuildGeometryInfo);
			rangeInfos.push_back({
				.primitiveCount = primitiveCount,
				.primitiveOffset = 0,
				.firstVertex = 0,
				.transformOffset = 0,
			});

			// Also keep a copy for later re-builds
			_dynamicBLASGeometries.push_back(baseGeometry);
			accelerationBuildGeometryInfo.pGeometries = &_dynamicBLASGeometries.back();
			_dynamicBLASBuildGeometryInfos.push_back(accelerationBuildGeometryInfo);
			_dynamicBLASBuildRangeInfos.push_back(rangeInfos.back());
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
			if(i >= staticBLASCount) {
				_dynamicBLASBuildGeometryInfos[i - staticBLASCount].dstAccelerationStructure = blas;
				//_dynamicBLASBuildGeometryInfos[i - staticBLASCount].srcAccelerationStructure = blas;
			}
			runningOffset += blasOffsets[i];
		}

		_blasScratchBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, scratchBufferSize);
		_blasScratchMemory.allocate(device, _blasScratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);
		size_t	   offset = 0;
		const auto scratchBufferAddr = _blasScratchBuffer.getDeviceAddress();
		for(size_t i = 0; i < buildInfos.size(); ++i) {
			buildInfos[i].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			if(i >= staticBLASCount) {
				_dynamicBLASBuildGeometryInfos[i - staticBLASCount].scratchData = {.deviceAddress = scratchBufferAddr + offset};
			}
			offset += buildSizesInfo[i].buildScratchSize;
			assert(buildInfos[i].geometryCount == 1); // See below! (pRangeInfos will be wrong in this case)
		}

		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos;
		for(auto& rangeInfo : rangeInfos)
			pRangeInfos.push_back(&rangeInfo); // FIXME: Only works because geometryCount is always 1 here.

		// Build all the bottom acceleration structure on the device via a one-time command buffer submission
		device.immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
			// Build all BLAS in a single call. Note: This might cause sync. issues if buffers are shared (We made sure the scratchBuffer is not.)
			vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(buildInfos.size()), buildInfos.data(), pRangeInfos.data());
		});
	}

	createTLAS(device);
}

void Scene::sortRenderers() {
	_registry.sort<MeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
	_registry.sort<SkinnedMeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
}

inline static [[nodiscard]] VkDeviceAddress getDeviceAddress(const Device& device, VkAccelerationStructureKHR accelerationStructure) {
	VkAccelerationStructureDeviceAddressInfoKHR BLASAddressInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = accelerationStructure,
	};
	return vkGetAccelerationStructureDeviceAddressKHR(device, &BLASAddressInfo);
}

void Scene::createTLAS(const Device& device) {
	QuickTimer qt("TLAS building");

	const auto& meshes = getMeshes();
	{
		auto instances = getRegistry().view<MeshRendererComponent>();
		for(auto& entity : instances) {
			auto&				 meshRendererComponent = _registry.get<MeshRendererComponent>(entity);
			auto				 tmp = glm::transpose(getGrobalTransform(_registry, _registry.get<NodeComponent>(entity)));
			VkTransformMatrixKHR transposedTransform = *reinterpret_cast<VkTransformMatrixKHR*>(&tmp); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			// Get the bottom acceleration structures' handle, which will be used during the top level acceleration build
			auto BLASDeviceAddress = getDeviceAddress(device, _bottomLevelAccelerationStructures[meshRendererComponent.meshIndex]);

			_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
				.transform = transposedTransform,
				.instanceCustomIndex = meshes[meshRendererComponent.meshIndex].indexIntoOffsetTable,
				.mask = 0xFF,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
				.accelerationStructureReference = BLASDeviceAddress,
			});
		}
	}
	{
		auto instances = getRegistry().view<SkinnedMeshRendererComponent>();
		for(auto& entity : instances) {
			auto&				 skinnedMeshRendererComponent = _registry.get<SkinnedMeshRendererComponent>(entity);
			auto				 tmp = glm::transpose(getGrobalTransform(_registry, _registry.get<NodeComponent>(entity)));
			VkTransformMatrixKHR transposedTransform = *reinterpret_cast<VkTransformMatrixKHR*>(&tmp); // glm matrices are column-major, VkTransformMatrixKHR is row-major
			auto				 BLASDeviceAddress = getDeviceAddress(device, _bottomLevelAccelerationStructures[skinnedMeshRendererComponent.blasIndex]);

			_accStructInstances.push_back(VkAccelerationStructureInstanceKHR{
				.transform = transposedTransform,
				.instanceCustomIndex = skinnedMeshRendererComponent.indexIntoOffsetTable,
				.mask = 0xFF,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
				.accelerationStructureReference = BLASDeviceAddress,
			});
		}
	}

	_accStructInstancesBuffer.create(
		device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		_accStructInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
	_accStructInstancesMemory.allocate(device, _accStructInstancesBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	copyViaStagingBuffer(device, _accStructInstancesBuffer, _accStructInstances);

	_instancesBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, _accStructInstances.size() * sizeof(InstanceData));
	_instancesMemory.allocate(device, _instancesBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	updateTransforms(device);

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

	const uint32_t							 TLASPrimitiveCount = static_cast<uint32_t>(_accStructInstances.size());
	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TLASBuildGeometryInfo, &TLASPrimitiveCount, &TLASBuildSizesInfo);

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
		.primitiveCount = TLASPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	device.immediateSubmitCompute(
		[&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

bool Scene::updateDynamicBLAS(const Device& device) {
	QuickTimer qt(_cpuBLASUpdateTimes);

	if(_dynamicBLASBuildGeometryInfos.empty())
		return false;

	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos;
	for(auto& rangeInfo : _dynamicBLASBuildRangeInfos)
		pRangeInfos.push_back(&rangeInfo);

	if(_updateQueryPools[0].newSampleFlag) {
		auto queryResults = _updateQueryPools[0].get();
		if(queryResults.size() >= 2 && queryResults[0].available && queryResults[1].available) {
			_dynamicBLASUpdateTimes.add(0.000001f * (queryResults[1].result - queryResults[0].result));
			_updateQueryPools[0].newSampleFlag = false;
		}
	}
	device.immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
		_updateQueryPools[0].reset(commandBuffer);
		_updateQueryPools[0].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, static_cast<uint32_t>(_dynamicBLASBuildGeometryInfos.size()), _dynamicBLASBuildGeometryInfos.data(), pRangeInfos.data());
		_updateQueryPools[0].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
	}); // FIXME: Too much synchronisation here (WaitQueueIdle)
	_updateQueryPools[0].newSampleFlag = true;
	return true;
}

void Scene::updateAccelerationStructureInstances(const Device& device) {
	for(size_t i = 0; i < _instancesData.size(); ++i) {
		auto t = glm::transpose(_instancesData[i].transform);
		_accStructInstances[i].transform = *reinterpret_cast<VkTransformMatrixKHR*>(&t);
	}
	copyViaStagingBuffer(device, _accStructInstancesBuffer, _accStructInstances);
}

void Scene::updateTLAS(const Device& device) {
	// TODO: Optimise (including with regards to the GPU sync.).
	QuickTimer qt(_cpuTLASUpdateTimes);

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

	if(_updateQueryPools[1].newSampleFlag) {
		auto queryResults = _updateQueryPools[1].get();
		if(queryResults.size() >= 2 && queryResults[0].available && queryResults[1].available) {
			_tlasUpdateTimes.add(0.000001f * (queryResults[1].result - queryResults[0].result));
			_updateQueryPools[1].newSampleFlag = false;
		}
	}
	device.immediateSubmitCompute([&](const CommandBuffer& commandBuffer) {
		_updateQueryPools[1].reset(commandBuffer);
		_updateQueryPools[1].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data());
		_updateQueryPools[1].writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
	});
	_updateQueryPools[1].newSampleFlag = true;
}

void Scene::updateTransforms(const Device& device) {
	// Sort by material then by mesh
	_registry.sort<MeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
	_registry.sort<SkinnedMeshRendererComponent>([](const auto& lhs, const auto& rhs) {
		if(lhs.materialIndex == rhs.materialIndex)
			return lhs.meshIndex < rhs.meshIndex;
		return lhs.materialIndex < rhs.materialIndex;
	});
	auto meshRenderers = _registry.view<MeshRendererComponent, NodeComponent>();
	auto skinnedMeshRenderers = _registry.view<SkinnedMeshRendererComponent, NodeComponent>();
	// TODO: Optimize by only updating dirtyNode when possible
	_instancesData.clear();
	_instancesData.reserve(meshRenderers.size_hint() + skinnedMeshRenderers.size_hint());
	for(auto&& [entity, meshRenderer, node] : meshRenderers.each())
		_instancesData.push_back({getGrobalTransform(_registry, node)});
	for(auto&& [entity, skinnedMeshRenderer, node] : skinnedMeshRenderers.each())
		_instancesData.push_back({getGrobalTransform(_registry, node)});

	copyViaStagingBuffer(device, _instancesBuffer, _instancesData);
}

entt::entity Scene::intersectNodes(Ray& ray) {
	Hit			 best;
	entt::entity bestNode = entt::null;
	const auto&	 meshes = getMeshes();
	// FIXME: Doesn't work on Skinned Meshes
	// Note: If we had cached/precomputed node bounds (without the need of meshes), we could speed this up a lot (basically an acceleration structure).
	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		if(auto* mesh = _registry.try_get<MeshRendererComponent>(entity); mesh != nullptr) {
			auto hit = intersect(ray, transform * meshes[mesh->meshIndex].getBounds());
			if(hit.hit && hit.depth < best.depth) {
				Ray localRay = glm::inverse(transform) * ray;
				hit = intersect(localRay, meshes[mesh->meshIndex]);
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

void Scene::removeFromHierarchy(entt::entity entity) {
	auto& node = _registry.get<NodeComponent>(entity);
	if(node.prev != entt::null)
		_registry.get<NodeComponent>(node.prev).next = node.next;
	if(node.next != entt::null)
		_registry.get<NodeComponent>(node.next).prev = node.prev;
	if(node.parent != entt::null) {
		auto& parentNode = _registry.get<NodeComponent>(node.parent);
		if(parentNode.first == entity)
			parentNode.first = node.next;
		--parentNode.children;
	}
	node.next = entt::null;
	node.prev = entt::null;
	node.parent = entt::null;
}

void Scene::addChild(entt::entity parent, entt::entity child) {
	assert(parent != child);
	auto& parentNode = _registry.get<NodeComponent>(parent);
	auto& childNode = _registry.get<NodeComponent>(child);
	assert(childNode.parent == entt::null); // We should probably handle this case, but we don't right now!
	if(parentNode.first == entt::null) {
		parentNode.first = child;
	} else {
		auto lastChild = parentNode.first;
		while(_registry.get<NodeComponent>(lastChild).next != entt::null) {
			lastChild = _registry.get<NodeComponent>(lastChild).next;
		}
		_registry.get<NodeComponent>(lastChild).next = child;
		childNode.prev = lastChild;
	}
	childNode.parent = parent;
	++parentNode.children;
	markDirty(parent);
	markDirty(child);
}

void Scene::addSibling(entt::entity target, entt::entity other) {
	assert(target != other);
	auto& targetNode = _registry.get<NodeComponent>(target);
	auto& otherNode = _registry.get<NodeComponent>(other);
	assert(otherNode.parent == entt::null); // We should probably handle this case, but we don't right now!
	auto& parentNode = _registry.get<NodeComponent>(targetNode.parent);
	++parentNode.children;
	otherNode.parent = targetNode.parent;
	if(targetNode.next != entt::null)
		_registry.get<NodeComponent>(targetNode.next).prev = other;
	otherNode.next = targetNode.next;
	otherNode.prev = target;
	targetNode.next = other;
}

void Scene::onDestroyNodeComponent(entt::registry& registry, entt::entity entity) {
	auto& node = registry.get<NodeComponent>(entity);
	print("Scene::onDestroyNodeComponent '{}' ({})\n", node.name, entity);
	removeFromHierarchy(entity);
	auto child = node.first;
	while(child != entt::null) {
		auto tmp = registry.get<NodeComponent>(child).next;
		registry.destroy(child); // Mmmh...?
		child = tmp;
	}
}
