#pragma once

#include <vector>

#include "Buffer.hpp"
#include "DeviceMemory.hpp"
#include "Vertex.hpp"

class Mesh {
  public:
    const std::vector<Vertex> _vertices = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}},
    };

    const std::vector<uint16_t> _indices = {0, 1, 2, 2, 3, 0};

    void init(VkDevice device) {
        const auto indexDataSize = getIndexByteSize();
        _indexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexDataSize);
        auto vertexDataSize = getVertexByteSize();
        _vertexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexDataSize);
    }

    void upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue) {
        stagingMemory.fill(_vertices);
        auto vertexDataSize = getVertexByteSize();
        _vertexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, vertexDataSize, queue);

        stagingMemory.fill(_indices);
        auto indexDataSize = getIndexByteSize();
        _indexBuffer.copyFromStagingBuffer(tmpCommandPool, stagingBuffer, indexDataSize, queue);
    }

    size_t getVertexByteSize() const {
        return sizeof(_vertices[0]) * _vertices.size();
    }

    size_t getIndexByteSize() const {
        return sizeof(_vertices[0]) * _vertices.size();
    }

    const Buffer& getVertexBuffer() const {
        return _vertexBuffer;
    }

    const Buffer& getIndexBuffer() const {
        return _indexBuffer;
    }

    void destroy() {
        _indexBuffer.destroy();
        _vertexBuffer.destroy();
    }

  private:
    Buffer _vertexBuffer;
    Buffer _indexBuffer;
};