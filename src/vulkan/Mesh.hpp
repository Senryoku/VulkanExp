#pragma once

#include <filesystem>
#include <vector>

#include "Buffer.hpp"
#include "DeviceMemory.hpp"
#include "Material.hpp"
#include "Vertex.hpp"

class Mesh {
  public:
	void init(const Device& device) {
		const auto indexDataSize = getIndexByteSize();
		const auto usageBitsForRayTracing = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		_indexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | usageBitsForRayTracing, indexDataSize);
		auto vertexDataSize = getVertexByteSize();
		_vertexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usageBitsForRayTracing, vertexDataSize);
		auto vertexBufferMemReq = _vertexBuffer.getMemoryRequirements();
		auto indexBufferMemReq = _indexBuffer.getMemoryRequirements();
		// FIXME: We should probably allocate larger buffers ahead of time rather than one per mesh.
		_memory.allocate(device, device.getPhysicalDevice().findMemoryType(vertexBufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
						 vertexBufferMemReq.size + indexBufferMemReq.size);
		vkBindBufferMemory(device, _vertexBuffer, _memory, 0);
		vkBindBufferMemory(device, _indexBuffer, _memory, vertexBufferMemReq.size);
	}

	void upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue) {
		stagingMemory.fill(_vertices);
		auto vertexDataSize = getVertexByteSize();
		_vertexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, vertexDataSize, queue);

		stagingMemory.fill(_indices);
		auto indexDataSize = getIndexByteSize();
		_indexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, indexDataSize, queue);
	}

	size_t getVertexByteSize() const { return sizeof(_vertices[0]) * _vertices.size(); }
	size_t getIndexByteSize() const { return sizeof(_vertices[0]) * _vertices.size(); }

	const Buffer& getVertexBuffer() const { return _vertexBuffer; }
	const Buffer& getIndexBuffer() const { return _indexBuffer; }

	void destroy() {
		_indexBuffer.destroy();
		_vertexBuffer.destroy();
		_memory.free();
	}

	const std::vector<Vertex>&	 getVertices() const { return _vertices; }
	const std::vector<uint16_t>& getIndices() const { return _indices; }
	std::vector<Vertex>&		 getVertices() { return _vertices; }
	std::vector<uint16_t>&		 getIndices() { return _indices; }

	bool loadOBJ(const std::filesystem::path& path);
	void normalizeVertices();
	void computeVertexNormals();

	// FIXME
	size_t	  materialIndex = 0;
	Material* material = nullptr;

  private:
	DeviceMemory _memory;
	Buffer		 _vertexBuffer;
	Buffer		 _indexBuffer;

	std::vector<Vertex>	  _vertices;
	std::vector<uint16_t> _indices;

	/* Box
		_vertices = {{-0.5f, -0.5f, 0.f}, {1.0f, 0.0f, 0.0f}}, {{0.5f, -0.5f, 0.f}, {0.0f, 1.0f, 0.0f}}, {{0.5f, 0.5f, 0.f}, {0.0f, 0.0f, 1.0f}}, {{-0.5f, 0.5f, 0.f},
	   {1.0f, 1.0f, 1.0f}};

		_indices = {0, 1, 2, 2, 3, 0};
	 */
};
