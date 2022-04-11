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

	std::string	  name;
	size_t		  blasIndex = -1;
	uint32_t	  indexIntoOffsetTable = -1;
	MaterialIndex defaultMaterialIndex{static_cast<uint32_t>(0)};

	bool isValid() const { return _indices.size() > 0; }

	void init(const Device& device) {
		if(_indexBuffer && _vertexBuffer) {
			_indexBuffer.destroy();
			_vertexBuffer.destroy();
		}

		if(!isValid())
			return;

		const auto indexDataSize = getIndexByteSize();
		const auto usageBitsForRayTracing = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		_indexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | usageBitsForRayTracing, indexDataSize);
		auto vertexDataSize = getVertexByteSize();
		_vertexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usageBitsForRayTracing, vertexDataSize);

		if(_skinJointsBuffer) {
			_skinJointsBuffer.destroy();
			_skinWeightsBuffer.destroy();
		}
		if(isSkinned()) {
			_skinJointsBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, getJointsByteSize());
			_skinWeightsBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, getWeightsByteSize());
		}
	}

	void upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue) {
		if(!isValid())
			return;

		stagingMemory.fill(_vertices);
		_vertexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, getVertexByteSize(), queue);

		stagingMemory.fill(_indices);
		_indexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, getIndexByteSize(), queue);

		if(isSkinned()) {
			stagingMemory.fill(getSkinVertexData().joints);
			_skinJointsBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, getJointsByteSize(), queue);
			stagingMemory.fill(getSkinVertexData().weights);
			_skinWeightsBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, getWeightsByteSize(), queue);
		}
	}

	inline size_t getVertexByteSize() const { return sizeof(_vertices[0]) * _vertices.size(); }
	inline size_t getIndexByteSize() const { return sizeof(_indices[0]) * _indices.size(); }

	inline const Buffer& getVertexBuffer() const { return _vertexBuffer; }
	inline const Buffer& getIndexBuffer() const { return _indexBuffer; }

	void destroy() {
		_indexBuffer.destroy();
		_vertexBuffer.destroy();
		_skinJointsBuffer.destroy();
		_skinWeightsBuffer.destroy();
	}

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
