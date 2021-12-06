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
	size_t getIndexByteSize() const { return sizeof(_indices[0]) * _indices.size(); }

	const Buffer& getVertexBuffer() const { return _vertexBuffer; }
	const Buffer& getIndexBuffer() const { return _indexBuffer; }

	void destroy() {
		_indexBuffer.destroy();
		_vertexBuffer.destroy();
	}

	const std::vector<Vertex>&	 getVertices() const { return _vertices; }
	const std::vector<uint32_t>& getIndices() const { return _indices; }
	std::vector<Vertex>&		 getVertices() { return _vertices; }
	std::vector<uint32_t>&		 getIndices() { return _indices; }

	bool loadOBJ(const std::filesystem::path& path);
	void normalizeVertices();
	void computeVertexNormals();

	// FIXME
	size_t	  materialIndex = 0;
	Material* material = nullptr;

	struct OffsetEntry {
		uint32_t vertexOffset;
		uint32_t indexOffset;
	};
	static void allocate(const Device& device, const std::vector<Mesh>& meshes) {
		uint32_t						  totalVertexSize = 0;
		uint32_t						  totalIndexSize = 0;
		std::vector<VkMemoryRequirements> memReqs;
		std::vector<OffsetEntry>		  offsetTable;
		for(const auto& m : meshes) {
			auto vertexBufferMemReq = m.getVertexBuffer().getMemoryRequirements();
			auto indexBufferMemReq = m.getIndexBuffer().getMemoryRequirements();
			memReqs.push_back(vertexBufferMemReq);
			memReqs.push_back(indexBufferMemReq);
			offsetTable.push_back(OffsetEntry{totalVertexSize / static_cast<uint32_t>(sizeof(Vertex)), totalIndexSize / static_cast<uint32_t>(sizeof(uint32_t))});
			totalVertexSize += static_cast<uint32_t>(vertexBufferMemReq.size);
			totalIndexSize += static_cast<uint32_t>(indexBufferMemReq.size);
		}
		VertexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[0].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalVertexSize);
		IndexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[1].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalIndexSize);
		for(size_t i = 0; i < meshes.size(); ++i) {
			vkBindBufferMemory(device, meshes[i].getVertexBuffer(), VertexMemory, NextVertexMemoryOffset);
			NextVertexMemoryOffset += memReqs[2 * i].size;
			vkBindBufferMemory(device, meshes[i].getIndexBuffer(), IndexMemory, NextIndexMemoryOffset);
			NextIndexMemoryOffset += memReqs[2 * i + 1].size;
		}
		// Create views to the entire dataset
		VertexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalVertexSize);
		vkBindBufferMemory(device, VertexBuffer, VertexMemory, 0);
		IndexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalIndexSize);
		vkBindBufferMemory(device, IndexBuffer, IndexMemory, 0);

		OffsetTableSize = sizeof(OffsetEntry) * offsetTable.size();
		OffsetTableBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, OffsetTableSize);
		OffsetTableMemory.allocate(device, OffsetTableBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		// FIXME: Remove VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT and use a staging buffer.
		OffsetTableMemory.fill(offsetTable.data(), offsetTable.size());
	}

	static void free() {
		OffsetTableBuffer.destroy();
		IndexBuffer.destroy();
		VertexBuffer.destroy();
		OffsetTableMemory.free();
		VertexMemory.free();
		IndexMemory.free();
	}
	// TODO: Move this to a Scene class, probably
	inline static DeviceMemory OffsetTableMemory;
	inline static DeviceMemory VertexMemory;
	inline static DeviceMemory IndexMemory;
	inline static size_t	   NextVertexMemoryOffset = 0;
	inline static size_t	   NextIndexMemoryOffset = 0;
	inline static Buffer	   OffsetTableBuffer;
	inline static Buffer	   VertexBuffer;
	inline static Buffer	   IndexBuffer;
	inline static uint32_t	   OffsetTableSize;

  private:
	Buffer _vertexBuffer;
	Buffer _indexBuffer;

	std::vector<Vertex>	  _vertices;
	std::vector<uint32_t> _indices;

	/* Box
		_vertices = {{-0.5f, -0.5f, 0.f}, {1.0f, 0.0f, 0.0f}}, {{0.5f, -0.5f, 0.f}, {0.0f, 1.0f, 0.0f}}, {{0.5f, 0.5f, 0.f}, {0.0f, 0.0f, 1.0f}}, {{-0.5f, 0.5f, 0.f},
	   {1.0f, 1.0f, 1.0f}};

		_indices = {0, 1, 2, 2, 3, 0};
	 */
};
