#include "Scene.hpp"

#include <fstream>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "JSON.hpp"
#include "Logger.hpp"
#include "STBImage.hpp"
#include <Base64.hpp>
#include <QuickTimer.hpp>
#include <Serialization.hpp>
#include <ThreadPool.hpp>
#include <vulkan/Material.hpp>

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
	auto canonicalPath = path.is_absolute() ? path.lexically_relative(std::filesystem::current_path()) : path.lexically_normal();
	if(canonicalPath.empty())
		canonicalPath = path;
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

template<typename T>
std::vector<T> extract(const JSON::value& object, const std::vector<std::vector<char>>& buffers, int accessorIndex) {
	const auto& accessor = object["accessors"][accessorIndex];
	if constexpr(std::is_same<T, float>()) {
		assert(Scene::ComponentType(accessor["componentType"].as<int>()) == Scene::ComponentType::Float);
		assert(accessor["type"].asString() == "SCALAR");
	}
	if constexpr(std::is_same<T, glm::vec2>()) {
		assert(Scene::ComponentType(accessor["componentType"].as<int>()) == Scene::ComponentType::Float);
		assert(accessor["type"].asString() == "VEC2");
	}
	if constexpr(std::is_same<T, glm::vec3>()) {
		assert(Scene::ComponentType(accessor["componentType"].as<int>()) == Scene::ComponentType::Float);
		assert(accessor["type"].asString() == "VEC3");
	}
	if constexpr(std::is_same<T, glm::vec4>()) {
		assert(Scene::ComponentType(accessor["componentType"].as<int>()) == Scene::ComponentType::Float);
		assert(accessor["type"].asString() == "VEC4");
	}
	if constexpr(std::is_same<T, glm::quat>()) {
		assert(Scene::ComponentType(accessor["componentType"].as<int>()) == Scene::ComponentType::Float);
		assert(accessor["type"].asString() == "VEC4");
	}
	const auto& bufferView = object["bufferViews"][accessor["bufferView"].as<int>()];
	auto		count = accessor["count"].as<int>();
	const auto& buffer = buffers[bufferView["buffer"].as<int>()];
	auto		bufferData = buffer.data();
	auto		cursor = accessor("byteOffset", 0) + bufferView("byteOffset", 0);
	int			defaultStride = sizeof(T);
	auto		stride = bufferView("byteStride", defaultStride);
	assert(stride >= sizeof(T));
	std::vector<T> data;
	for(int i = 0; i < count; ++i) {
		data.push_back(*reinterpret_cast<const T*>(bufferData + cursor));
		cursor += stride;
	}
	return data;
}

template<typename T>
JointIndices extractJoints(const char* data, size_t cursor) {
	JointIndices ji;
	for(int i = 0; i < 4; ++i)
		ji.indices[i] = reinterpret_cast<const T*>(data + cursor)[i];
	return ji;
}

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

			auto initAccessor = [&](const std::string& name, const char** bufferData, size_t* cursor, size_t* stride, const int defaultStride = 4 * sizeof(float),
									ComponentType expectedComponentType = ComponentType::Float, const std::string& expectedType = "VEC4") {
				const auto& accessor = object["accessors"][p["attributes"][name].as<int>()];
				assert(expectedComponentType == ComponentType::Any || static_cast<ComponentType>(accessor["componentType"].as<int>()) == expectedComponentType);
				assert(accessor["type"].as<std::string>() == expectedType);
				const auto& bufferView = object["bufferViews"][accessor["bufferView"].as<int>()];
				const auto& buffer = buffers[bufferView["buffer"].as<int>()];
				*bufferData = buffer.data();
				*cursor = accessor("byteOffset", 0) + bufferView("byteOffset", 0);
				*stride = bufferView("byteStride", defaultStride);
			};

			const char* normalBufferData = nullptr;
			size_t		normalCursor = 0;
			size_t		normalStride = 0;
			if(p["attributes"].contains("NORMAL"))
				initAccessor("NORMAL", &normalBufferData, &normalCursor, &normalStride, sizeof(glm::vec3), ComponentType::Float, "VEC3");

			const char* tangentBufferData = nullptr;
			size_t		tangentCursor = 0;
			size_t		tangentStride = 0;
			if(p["attributes"].contains("TANGENT"))
				initAccessor("TANGENT", &tangentBufferData, &tangentCursor, &tangentStride);
			// TODO: Compute tangents if not present in file.

			const char* texCoordBufferData = nullptr;
			size_t		texCoordCursor = 0;
			size_t		texCoordStride = 0;
			if(p["attributes"].contains("TEXCOORD_0"))
				initAccessor("TEXCOORD_0", &texCoordBufferData, &texCoordCursor, &texCoordStride, 2 * sizeof(float), ComponentType::Float, "VEC2");

			bool		skinnedMesh = p["attributes"].contains("WEIGHTS_0");
			const char* weightsBufferData = nullptr;
			size_t		weightsCursor = 0;
			size_t		weightsStride = 0;
			const char* jointsBufferData = nullptr;
			size_t		jointsCursor = 0;
			size_t		jointsStride = 0;

			ComponentType jointsIndexType = ComponentType::UnsignedShort;
			if(skinnedMesh) {
				initAccessor("WEIGHTS_0", &weightsBufferData, &weightsCursor, &weightsStride);
				assert(p["attributes"].contains("JOINTS_0"));
				jointsIndexType = static_cast<ComponentType>(object["accessors"][p["attributes"]["JOINTS_0"].as<int>()]["componentType"].as<int>());
				initAccessor("JOINTS_0", &jointsBufferData, &jointsCursor, &jointsStride, 4 * sizeof(JointIndex), ComponentType::Any);
			}
			std::vector<glm::vec4>	  weights;
			std::vector<JointIndices> joints;

			if(positionAccessor["type"].asString() == "VEC3") {
				assert(static_cast<ComponentType>(positionAccessor["componentType"].as<int>()) == ComponentType::Float); // TODO
				size_t positionCursor = positionAccessor("byteOffset", 0) + positionBufferView("byteOffset", 0);
				size_t positionStride = positionBufferView("byteStride", static_cast<int>(3 * sizeof(float)));

				mesh.getVertices().reserve(positionAccessor["count"].as<int>());
				for(size_t i = 0; i < positionAccessor["count"].as<int>(); ++i) {
					Vertex v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
					v.pos = *reinterpret_cast<const glm::vec3*>(positionBuffer.data() + positionCursor);
					positionCursor += positionStride;

					if(normalBufferData) {
						v.normal = *reinterpret_cast<const glm::vec3*>(normalBufferData + normalCursor);
						normalCursor += normalStride;
					}

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
						if(jointsIndexType != ComponentType::UnsignedShort) {
							switch(jointsIndexType) {
								case ComponentType::Byte: joints.push_back(extractJoints<int8_t>(jointsBufferData, jointsCursor)); break;
								case ComponentType::UnsignedByte: joints.push_back(extractJoints<uint8_t>(jointsBufferData, jointsCursor)); break;
								case ComponentType::Short: joints.push_back(extractJoints<int16_t>(jointsBufferData, jointsCursor)); break;
								case ComponentType::Int: joints.push_back(extractJoints<int32_t>(jointsBufferData, jointsCursor)); break;
								case ComponentType::UnsignedInt: joints.push_back(extractJoints<uint32_t>(jointsBufferData, jointsCursor)); break;
							}
						} else
							joints.push_back(*reinterpret_cast<const JointIndices*>(jointsBufferData + jointsCursor));
						jointsCursor += jointsStride;
					}

					mesh.getVertices().push_back(v);
				}
			} else {
				error("Error: Unsupported accessor type '{}'.", positionAccessor["type"].asString());
			}

			if(skinnedMesh)
				mesh.setSkinVertexData(SkinVertexData{weights, joints});

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

			if(p.contains("indices")) {
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
			} else {
				// Compute indices ourselves
				mesh.getIndices().reserve(mesh.getVertices().size() / 3);
				for(size_t i = 0; i < mesh.getVertices().size(); i += 3) {
					mesh.getIndices().push_back(i + 0);
					mesh.getIndices().push_back(i + 1);
					mesh.getIndices().push_back(i + 2);
				}
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
																					   .skinIndex = SkinIndex(node["skin"].as<int>() + _skins.size()),
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
			std::vector<glm::mat4>	  inverseBindMatrices = extract<glm::mat4>(object, buffers, skin["inverseBindMatrices"].as<int>());
			std::vector<entt::entity> joints;
			for(const auto& nodeIndex : skin["joints"])
				joints.push_back(entities[nodeIndex.as<int>()]);
			_skins.push_back({inverseBindMatrices, joints});
		}

	if(object.contains("animations"))
		for(const auto& anim : object["animations"]) {
			SkeletalAnimationClip animation;
			entt::entity		  rootNode = entt::null;
			for(const auto& channel : anim["channels"]) {
				auto node = entities[channel["target"]["node"].as<int>()];
				if(rootNode == entt::null || isAncestor(node, rootNode))
					rootNode = node;
				auto  path = SkeletalAnimationClip::parsePath(channel["target"]["path"].asString());
				auto& sampler = anim["samplers"][channel["sampler"].as<int>()];
				auto  input = extract<float>(object, buffers, sampler["input"].as<int>());
				auto& nodeAnim = animation.nodeAnimations[node];
				nodeAnim.entity = node;
				auto interpolation = sampler.contains("interpolation") ? SkeletalAnimationClip::parseInterpolation(sampler["interpolation"].asString())
																	   : SkeletalAnimationClip::Interpolation::Linear;
				switch(path) {
					case SkeletalAnimationClip::Path::Translation: {
						nodeAnim.translationKeyFrames.interpolation = interpolation;
						assert(object["accessors"][sampler["output"].as<int>()]["type"].asString() == "VEC3");
						auto output = extract<glm::vec3>(object, buffers, sampler["output"].as<int>());
						for(int i = 0; i < input.size(); ++i)
							nodeAnim.translationKeyFrames.add(input[i], output[i]);
						break;
					}
					case SkeletalAnimationClip::Path::Rotation: {
						nodeAnim.rotationKeyFrames.interpolation = interpolation;
						assert(object["accessors"][sampler["output"].as<int>()]["type"].asString() == "VEC4");
						auto output = extract<glm::quat>(object, buffers, sampler["output"].as<int>()); // FIXME: quats are probably not in the expected format
						for(int i = 0; i < input.size(); ++i)
							nodeAnim.rotationKeyFrames.add(input[i], output[i]);
						break;
					}
					case SkeletalAnimationClip::Path::Scale: {
						nodeAnim.scaleKeyFrames.interpolation = interpolation;
						assert(object["accessors"][sampler["output"].as<int>()]["type"].asString() == "VEC3");
						auto output = extract<glm::vec3>(object, buffers, sampler["output"].as<int>());
						for(int i = 0; i < input.size(); ++i)
							nodeAnim.scaleKeyFrames.add(input[i], output[i]);
						break;
					}
					case SkeletalAnimationClip::Path::Weights: {
						nodeAnim.weightsKeyFrames.interpolation = interpolation;
						if(object["accessors"][sampler["output"].as<int>()]["type"].asString() != "VEC4") {
							warn("Ignoring SkeletalAnimation::Path::Weights of type '{}' (expected 'VEC4').\n",
								 object["accessors"][sampler["output"].as<int>()]["type"].asString());
							break;
						}
						auto output = extract<glm::vec4>(object, buffers, sampler["output"].as<int>());
						for(int i = 0; i < input.size(); ++i)
							nodeAnim.weightsKeyFrames.add(input[i], output[i]);
						break;
					}
				}
			}
			if(rootNode != entt::null) {
				if(!_registry.try_get<AnimationComponent>(rootNode)) {
					auto& animComp = _registry.emplace<AnimationComponent>(rootNode);
					animComp.animationIndex = AnimationIndex(Animations.size());
				}
			}
			Animations.push_back(animation);
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
			auto		imageIndex = texture["source"].as<int>();
			const auto& image = json["images"][imageIndex];
			if(image.contains("uri")) {
				Textures.push_back(Texture{
					.source = path.parent_path() / json["images"][texture["source"].as<int>()]["uri"].asString(),
					.format = VK_FORMAT_R8G8B8A8_SRGB,
					.samplerDescription = json["samplers"][texture("sampler", 0)].asObject(), // When undefined, a sampler with repeat wrapping and auto filtering should be used.
				});
			} else {
				auto		bufferViewIndex = image["bufferView"].as<int>();
				const auto& bufferView = json["bufferViews"][bufferViewIndex];
				auto		mimeType = image["mimeType"].asString();
				warn("Scene::loadTextures: Embeded textures are not yet supported (replaced by blank image). Type: '{}', BufferView: '{}'.\n", mimeType, bufferViewIndex);
				Textures.push_back(Texture{
					.source = "data/blank.png",
					.format = VK_FORMAT_R8G8B8A8_SRGB,
					.samplerDescription = json["samplers"][texture("sampler", 0)].asObject(), // When undefined, a sampler with repeat wrapping and auto filtering should be used.
				});
			}
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
		if(auto* comp = _registry.try_get<SkinnedMeshRendererComponent>(entity); comp != nullptr) {
			warn("Scene::save: SkinnedMeshRendererComponents are not supported yet.");
		}
		if(auto* comp = _registry.try_get<AnimationComponent>(entity); comp != nullptr) {
			warn("Scene::save: AnimationComponents are not supported yet.");
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
			mesh.computeBounds();
		}

		// Find root (FIXME: There's probably a better way to do this. Should we order the nodes when saving so the root is always the first node in the array? It's also probably a
		// win for performance, mmh...)
		for(auto e : entities)
			if(_registry.get<NodeComponent>(e).parent == entt::null) { // Note: This means that we don't support out-of-tree entities, but it's probably fine.
				_root = e;
				break;
			}
		markDirty(_root);
		computeBounds();
		return true;
	}
	return false;
}

bool Scene::update(float deltaTime) {
	QuickTimer qt(_updateTimes);
	bool	   hierarchicalChanges = false;
	if(!_dirtyNodes.empty()) {
		// Update all cached globalTransform
		// FIXME: We could restrict ourselves to _dirtyNodes by computing they common ancestor for example (simply iterating over _dirtyNodes will traverse the same nodes multiple
		// times)
		auto&																	   node = _registry.get<NodeComponent>(_root);
		std::function<void(const glm::mat4& parentTransform, NodeComponent& node)> updateChildrenGlobalTransforms = [&](const glm::mat4& parentTransform,
																														NodeComponent&	 parentNode) {
			auto child = parentNode.first;
			while(child != entt::null) {
				auto& childNode = _registry.get<NodeComponent>(child);
				childNode.globalTransform = parentTransform * childNode.transform;
				updateChildrenGlobalTransforms(childNode.globalTransform, childNode);
				child = childNode.next;
			}
		};
		updateChildrenGlobalTransforms(node.globalTransform, node);
		computeBounds();
		_dirtyNodes.clear();
		hierarchicalChanges = true;
	}

	return hierarchicalChanges;
}

bool Scene::isAncestor(entt::entity ancestor, entt::entity entity) const {
	const auto& node = _registry.get<NodeComponent>(entity);
	auto		parent = node.parent;
	while(parent != entt::null) {
		if(parent == ancestor)
			return true;
		parent = _registry.get<NodeComponent>(parent).parent;
	}
	return false;
}

// Computes the global transform of a node
// Prefer using the cached globalTransform directly on the node!
glm::mat4 Scene::getGlobalTransform(const NodeComponent& node) const {
	auto transform = node.transform;
	auto parent = node.parent;
	while(parent != entt::null) {
		const auto& parentNode = _registry.get<NodeComponent>(parent);
		transform = parentNode.transform * transform;
		parent = parentNode.parent;
	}
	return transform;
}

entt::entity Scene::intersectNodes(const Ray& ray) {
	Hit			 best;
	entt::entity bestNode = entt::null;
	const auto&	 meshes = getMeshes();
	// FIXME: Uses base geometry of SkinnedMesh, which will lead to imprecise results
	// Note: If we had cached/precomputed node bounds (without the need of meshes), we could speed this up a lot (basically an acceleration structure).
	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		auto* mesh = _registry.try_get<MeshRendererComponent>(entity);
		auto* skinnedMesh = _registry.try_get<SkinnedMeshRendererComponent>(entity);
		if(auto meshIndex = mesh ? mesh->meshIndex : skinnedMesh ? skinnedMesh->meshIndex : InvalidMeshIndex; meshIndex != InvalidMeshIndex) {
			auto hit = intersect(ray, transform * meshes[meshIndex].getBounds());
			if(hit.hit && hit.depth < best.depth) {
				Ray localRay = glm::inverse(transform) * ray;
				hit = intersect(localRay, meshes[meshIndex]);
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

const Bounds& Scene::computeBounds() {
	bool init = false;

	forEachNode([&](entt::entity entity, glm::mat4 transform) {
		auto* mesh = _registry.try_get<MeshRendererComponent>(entity);
		auto* skinnedMesh = _registry.try_get<SkinnedMeshRendererComponent>(entity);
		if(mesh || skinnedMesh) {
			const auto& bounds = _meshes[mesh ? mesh->meshIndex : skinnedMesh->meshIndex].getBounds();
			if(!init) {
				_bounds = transform * bounds;
				init = true;
			} else
				_bounds += transform * bounds;
		}
	});

	return _bounds;
}

void Scene::visitNode(entt::entity entity, glm::mat4 transform, const std::function<void(entt::entity entity, glm::mat4)>& call) {
	const auto& node = _registry.get<NodeComponent>(entity);
	transform = transform * node.transform;
	for(auto c = node.first; c != entt::null; c = _registry.get<NodeComponent>(c).next)
		visitNode(c, transform, call);

	call(entity, transform);
};

void Scene::free() {
	for(auto& m : getMeshes())
		m.destroy();
}
