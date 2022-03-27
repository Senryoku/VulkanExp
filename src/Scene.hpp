#pragma once

#include <filesystem>

#include <Mesh.hpp>
#include <Raytracing.hpp>
#include <RollingBuffer.hpp>
#include <TaggedType.hpp>
#include <entt.hpp>

// TODO: Move this :)
inline std::vector<Material> Materials;

struct NodeComponent {
	std::string	 name{"Unamed Node"};
	glm::mat4	 transform{1.0f};
	glm::mat4	 globalTransform{1.0f}; // Cached Global Transform: Do not modify directly!
	std::size_t	 children{0};
	entt::entity first{entt::null};
	entt::entity prev{entt::null};
	entt::entity next{entt::null};
	entt::entity parent{entt::null};
};

struct MeshIndexTag {};
using MeshIndex = TaggedIndex<uint32_t, MeshIndexTag>;
inline static const MeshIndex InvalidMeshIndex{static_cast<uint32_t>(-1)};
struct SkinIndexTag {};
using SkinIndex = TaggedIndex<uint32_t, SkinIndexTag>;
inline static const SkinIndex InvalidSkinIndex{static_cast<uint32_t>(-1)};

struct MeshRendererComponent {
	MeshIndex	  meshIndex = InvalidMeshIndex; // FIXME: Use something else.
	MaterialIndex materialIndex = InvalidMaterialIndex;
};

struct SkinnedMeshRendererComponent {
	MeshIndex	  meshIndex = InvalidMeshIndex; // FIXME: Use something else.
	MaterialIndex materialIndex = InvalidMaterialIndex;
	SkinIndex	  skinIndex = InvalidSkinIndex;
	size_t		  blasIndex = static_cast<size_t>(-1);
	uint32_t	  indexIntoOffsetTable = 0;
};

struct AnimationComponent {
	float		   time = 0;
	AnimationIndex animationIndex = InvalidAnimationIndex;
};

struct Skin {
	std::vector<glm::mat4>	  inverseBindMatrices;
	std::vector<entt::entity> joints;
};

class Scene {
  public:
	enum class RenderingMode {
		Points = 0,
		Line = 1,
		LineLoop = 2,
		LineStrip = 3,
		Triangles = 4,
		TriangleStrip = 5,
		TriangleFan = 6
	};

	enum class ComponentType {
		Byte = 5120,
		UnsignedByte = 5121,
		Short = 5122,
		UnsignedShort = 5123,
		Int = 5124,
		UnsignedInt = 5125,
		Float = 5126,
		Double = 5130,
	};

	Scene();
	Scene(const std::filesystem::path& path);
	~Scene();

	bool load(const std::filesystem::path& path);
	bool loadglTF(const std::filesystem::path& path);
	bool loadOBJ(const std::filesystem::path& path);
	bool loadMaterial(const std::filesystem::path& path);
	bool loadScene(const std::filesystem::path& path);

	bool save(const std::filesystem::path& path);

	entt::registry&					getRegistry() { return _registry; }
	const entt::registry&			getRegistry() const { return _registry; }
	const RollingBuffer<float>&		getUpdateTimes() const { return _updateTimes; }
	inline entt::entity				getRoot() const { return _root; }
	inline std::vector<Mesh>&		getMeshes() { return _meshes; }
	inline const std::vector<Mesh>& getMeshes() const { return _meshes; }
	inline std::vector<Skin>&		getSkins() { return _skins; }
	inline const std::vector<Skin>& getSkins() const { return _skins; }

	inline void markDirty(entt::entity node) { _dirtyNodes.push_back(node); }
	bool		update(float deltaTime);

	void removeFromHierarchy(entt::entity);
	void addChild(entt::entity parent, entt::entity child);
	void addSibling(entt::entity target, entt::entity other);

	bool	  isAncestor(entt::entity ancestor, entt::entity entity) const;
	glm::mat4 getGlobalTransform(const NodeComponent& node) const;

	entt::entity intersectNodes(const Ray& ray);

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	const Bounds&		 computeBounds() {
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

	// Depth-First traversal of the node hierarchy
	// Callback will be call for each entity with the entity and its world transformation as parameters.
	void forEachNode(const std::function<void(entt::entity entity, glm::mat4)>& call) { visitNode(getRoot(), glm::mat4(1.0f), call); }

	Mesh& operator[](MeshIndex index) {
		assert(index != InvalidMeshIndex);
		return _meshes[index];
	}
	const Mesh& operator[](MeshIndex index) const {
		assert(index != InvalidMeshIndex);
		return _meshes[index];
	}

	void free();

  private:
	std::vector<Mesh> _meshes;
	std::vector<Skin> _skins;

	entt::registry			  _registry;
	entt::entity			  _root = entt::null;
	std::vector<entt::entity> _dirtyNodes; // FIXME: May not be useful anymore.

	Bounds				 _bounds;
	RollingBuffer<float> _updateTimes;

	bool loadMaterial(const JSON::value& mat, uint32_t textureOffset);
	bool loadTextures(const std::filesystem::path& path, const JSON::value& json);

	// Called on NodeComponent destruction
	void onDestroyNodeComponent(entt::registry& registry, entt::entity node);

	// Used for depth-first traversal of the node hierarchy
	void visitNode(entt::entity entity, glm::mat4 transform, const std::function<void(entt::entity entity, glm::mat4)>& call) {
		const auto& node = _registry.get<NodeComponent>(entity);
		transform = transform * node.transform;
		for(auto c = node.first; c != entt::null; c = _registry.get<NodeComponent>(c).next)
			visitNode(c, transform, call);

		call(entity, transform);
	};
};

JSON::value toJSON(const NodeComponent&);
