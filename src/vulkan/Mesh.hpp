#pragma once

#include <filesystem>
#include <vector>

#include "Bounds.hpp"
#include "Buffer.hpp"
#include "DeviceMemory.hpp"
#include "Material.hpp"
#include "Vertex.hpp"

using JointIndex = uint16_t;

struct JointIndices {
	std::array<JointIndex, 4> indices;
};

struct SkinVertexData {
	std::vector<glm::vec4>	  weights;
	std::vector<JointIndices> joints;
};

class Mesh {
  public:
	Mesh() = default;
	Mesh(const Mesh&) = delete;
	Mesh(Mesh&&) noexcept = default;

	std::string	  name = "AnonMesh";
	bool		  dynamic = false; // Will allocate fixed sized buffers for vertices/indices instead of the initial minimum to allow mutating geometries
	size_t		  blasIndex = -1;
	uint32_t	  indexIntoOffsetTable = -1;
	MaterialIndex defaultMaterialIndex{static_cast<uint32_t>(0)};

	// FIXME: This will not fit in a 32bit address space... And OffsetTable uses 32bit addresses.
	static constexpr uint32_t DynamicVertexCapacity =
		10000; // 32768 / 2; // Arbitrary custom capacity for dynamic meshes. Could be per-mesh and user controled instead of a static value.
	static constexpr uint32_t DynamicIndexCapacity = 32768 / 2;

	inline bool isValid() const { return _indices.size() > 0; }

	void init(const Device& device);
	void upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue);
	void destroy();

	inline size_t getVertexByteSize() const {
		assert(!dynamic || _vertices.size() < DynamicVertexCapacity);
		return sizeof(Vertex) * (dynamic ? DynamicVertexCapacity : _vertices.size());
	}
	inline size_t getIndexByteSize() const {
		assert(!dynamic || _indices.size() < DynamicIndexCapacity);
		return sizeof(uint32_t) * (dynamic ? DynamicIndexCapacity : _indices.size());
	}

	inline const Buffer& getVertexBuffer() const { return _vertexBuffer; }
	inline const Buffer& getIndexBuffer() const { return _indexBuffer; }

	inline const std::vector<Vertex>&	getVertices() const { return _vertices; }
	inline const std::vector<uint32_t>& getIndices() const { return _indices; }
	inline std::vector<Vertex>&			getVertices() { return _vertices; }
	inline std::vector<uint32_t>&		getIndices() { return _indices; }

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	const Bounds&		 computeBounds();

	inline bool					 isSkinned() const { return _skinVertexData.has_value(); }
	inline const SkinVertexData& getSkinVertexData() const { return _skinVertexData.value(); }
	inline SkinVertexData&		 getSkinVertexData() { return _skinVertexData.value(); }
	inline void					 setSkinVertexData(const SkinVertexData& s) { _skinVertexData.emplace(s); }
	inline const Buffer&		 getSkinJointsBuffer() const { return _skinJointsBuffer; }
	inline const Buffer&		 getSkinWeightsBuffer() const { return _skinWeightsBuffer; }
	inline size_t				 getJointsByteSize() const { return sizeof(getSkinVertexData().joints[0]) * getSkinVertexData().joints.size(); }
	inline size_t				 getWeightsByteSize() const { return sizeof(getSkinVertexData().weights[0]) * getSkinVertexData().weights.size(); }

	void normalizeVertices();
	void computeVertexNormals();

  private:
	Buffer _vertexBuffer;
	Buffer _indexBuffer;

	std::vector<Vertex>	  _vertices;
	std::vector<uint32_t> _indices;

	std::optional<SkinVertexData> _skinVertexData{};
	Buffer						  _skinJointsBuffer;
	Buffer						  _skinWeightsBuffer;

	Bounds _bounds;
};
