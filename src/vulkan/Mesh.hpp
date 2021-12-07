#pragma once

#include <filesystem>
#include <vector>

#include "Buffer.hpp"
#include "DeviceMemory.hpp"
#include "Material.hpp"
#include "Vertex.hpp"

class SubMesh {
  public:
	SubMesh() = default;
	SubMesh(const SubMesh&) = delete;
	SubMesh(SubMesh&&) noexcept = default;

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

	bool loadOBJ(const std::filesystem::path& path);
	void normalizeVertices();
	void computeVertexNormals();

	// FIXME: Probably only use the index?
	size_t		materialIndex = 0;
	Material*	material = nullptr;
	std::string name;

  private:
	Buffer _vertexBuffer;
	Buffer _indexBuffer;

	std::vector<Vertex>	  _vertices;
	std::vector<uint32_t> _indices;
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

	///////////////////////////////////////////////////////////////////////////////////////
	// Static stuff I should get rid of :)
	// TODO: Move this to a Scene class, probably
	struct OffsetEntry {
		uint32_t materialIndex;
		uint32_t vertexOffset;
		uint32_t indexOffset;
	};
	inline static DeviceMemory OffsetTableMemory;
	inline static DeviceMemory VertexMemory;
	inline static DeviceMemory IndexMemory;
	inline static size_t	   NextVertexMemoryOffset = 0;
	inline static size_t	   NextIndexMemoryOffset = 0;
	inline static Buffer	   OffsetTableBuffer;
	inline static Buffer	   VertexBuffer;
	inline static Buffer	   IndexBuffer;
	inline static uint32_t	   OffsetTableSize;
	// Allocate memory for ALL currently loaded Meshes (FIXME: Move this elsewhere)
	static void allocate(const Device& device, const std::vector<Mesh>& meshes);
	static void free() {
		OffsetTableBuffer.destroy();
		IndexBuffer.destroy();
		VertexBuffer.destroy();
		OffsetTableMemory.free();
		VertexMemory.free();
		IndexMemory.free();
	}
	///////////////////////////////////////////////////////////////////////////////////////
};
