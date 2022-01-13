#pragma once

#include <filesystem>
#include <vector>

#include "Buffer.hpp"
#include "DeviceMemory.hpp"
#include "Material.hpp"
#include "Vertex.hpp"

struct Bounds {
	glm::vec3 min;
	glm::vec3 max;

	inline Bounds& operator+=(const Bounds& o) {
		min = glm::min(min, o.min);
		max = glm::max(max, o.max);
		return *this;
	}

	inline Bounds operator+(const Bounds& o) {
		return {
			.min = glm::min(min, o.min),
			.max = glm::max(max, o.max),
		};
	}
};

inline Bounds operator*(const glm::mat4& transform, const Bounds& b) {
	return {
		.min = glm::vec3(transform * glm::vec4(b.min, 1.0f)),
		.max = glm::vec3(transform * glm::vec4(b.max, 1.0f)),
	};
}

class SubMesh {
  public:
	SubMesh() = default;
	SubMesh(const SubMesh&) = delete;
	SubMesh(SubMesh&&) noexcept = default;

	std::string name;
	uint32_t	indexIntoOffsetTable = -1;
	size_t		materialIndex = 0;
	Material*	material = nullptr; // FIXME: Probably only use the index?

	void init(const Device& device) {
		const auto indexDataSize = getIndexByteSize();
		const auto usageBitsForRayTracing = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		_indexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | usageBitsForRayTracing, indexDataSize);
		auto vertexDataSize = getVertexByteSize();
		_vertexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usageBitsForRayTracing, vertexDataSize);
	}

	void upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue) {
		stagingMemory.fill(_vertices);
		auto vertexDataSize = getVertexByteSize();
		_vertexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, vertexDataSize, queue);

		stagingMemory.fill(_indices);
		auto indexDataSize = getIndexByteSize();
		_indexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, indexDataSize, queue);
	}

	inline size_t getVertexByteSize() const { return sizeof(_vertices[0]) * _vertices.size(); }
	inline size_t getIndexByteSize() const { return sizeof(_indices[0]) * _indices.size(); }

	inline const Buffer& getVertexBuffer() const { return _vertexBuffer; }
	inline const Buffer& getIndexBuffer() const { return _indexBuffer; }

	void destroy() {
		_indexBuffer.destroy();
		_vertexBuffer.destroy();
	}

	inline const std::vector<Vertex>&	getVertices() const { return _vertices; }
	inline const std::vector<uint32_t>& getIndices() const { return _indices; }
	inline std::vector<Vertex>&			getVertices() { return _vertices; }
	inline std::vector<uint32_t>&		getIndices() { return _indices; }

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	void				 computeBounds();

	bool loadOBJ(const std::filesystem::path& path);
	void normalizeVertices();
	void computeVertexNormals();

  private:
	Buffer _vertexBuffer;
	Buffer _indexBuffer;

	std::vector<Vertex>	  _vertices;
	std::vector<uint32_t> _indices;

	Bounds _bounds;
};

class Mesh {
  public:
	Mesh() = default;
	Mesh(const Mesh&) = delete;
	Mesh(Mesh&&) = default;

	std::string			 name;
	std::vector<SubMesh> SubMeshes;

	void destroy() { SubMeshes.clear(); }

	~Mesh() { destroy(); }

	inline const Bounds& getBounds() const { return _bounds; }
	inline void			 setBounds(const Bounds& b) { _bounds = b; }
	inline const Bounds& computeBounds() {
		_bounds = SubMeshes[0].getBounds();
		for(const auto& sm : SubMeshes)
			_bounds += sm.getBounds();
		return _bounds;
	}

  private:
	Bounds _bounds;
};
