#include "Mesh.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <JSON.hpp>
#include <Logger.hpp>
#include <stringutils.hpp>

bool SubMesh::loadOBJ(const std::filesystem::path& path) {
	std::ifstream file{path};

	_vertices.clear();
	_indices.clear();
	_vertices.reserve(65536);
	_indices.reserve(4 * 65536);

	std::string line;
	Vertex		v{glm::vec3{0.0, 0.0, 0.0}, glm::vec3{1.0, 1.0, 1.0}};
	while(std::getline(file, line)) {
		// Ignore empty lines and comments
		if(line.empty() || line[line.find_first_not_of(" \t")] == '#')
			continue;
		if(line[0] == 'v') {
			char* cur = line.data() + 2;
			for(size_t i = 0; i < 3; ++i)
				v.pos[i] = static_cast<float>(std::strtof(cur, &cur));
			_vertices.push_back(v);
		} else if(line[0] == 'f') {
			char* cur = line.data() + 2;
			for(size_t i = 0; i < 3; ++i)												   // Supports only triangles.
				_indices.push_back(static_cast<uint32_t>(std::strtol(cur, &cur, 10) - 1)); // Indices starts at 1 in .obj
		} else {
			warn("Unsupported OBJ command: '{}' (Full line: '{}')\n", line[0], line);
		}
	}
	return true;
}

void SubMesh::normalizeVertices() {
	glm::vec3 acc{0};
	for(const auto& v : _vertices)
		acc += v.pos;
	acc /= _vertices.size();
	for(auto& v : _vertices)
		v.pos -= acc;
}

void SubMesh::computeVertexNormals() {
	// Here, normals are the average of adjacent triangles' normals
	// (so we have exactly one normal per vertex)
	for(auto& v : _vertices)
		v.normal = glm::vec3{0.0f};

	for(size_t i = 0; i < _indices.size();) {
		// Norm of this triangle
		glm::vec3 norm = glm::normalize(glm::cross(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos, _vertices[_indices[i + 2]].pos - _vertices[_indices[i]].pos));
		// Add it to each vertex
		for(size_t j = 0; j < 3; ++j) {
			_vertices[_indices[i]].normal += norm;
			++i;
		}
	}
	// Average norms
	for(auto& v : _vertices)
		v.normal = glm::normalize(v.normal);
}

void SubMesh::computeBounds() {
	Bounds b{.min = _vertices[0].pos, .max = _vertices[0].pos};
	for(const auto& v : _vertices) {
		b.min = glm::min(b.min, v.pos);
		b.max = glm::max(b.max, v.pos);
	}
	_bounds = b;
}

void Mesh::allocate(const Device& device, const std::vector<Mesh>& meshes) {
	uint32_t						  totalVertexSize = 0;
	uint32_t						  totalIndexSize = 0;
	std::vector<VkMemoryRequirements> memReqs;
	std::vector<OffsetEntry>		  offsetTable;
	for(const auto& m : meshes) {
		for(const auto& sm : m.SubMeshes) {
			auto vertexBufferMemReq = sm.getVertexBuffer().getMemoryRequirements();
			auto indexBufferMemReq = sm.getIndexBuffer().getMemoryRequirements();
			memReqs.push_back(vertexBufferMemReq);
			memReqs.push_back(indexBufferMemReq);
			offsetTable.push_back(OffsetEntry{static_cast<uint32_t>(sm.materialIndex), totalVertexSize / static_cast<uint32_t>(sizeof(Vertex)),
											  totalIndexSize / static_cast<uint32_t>(sizeof(uint32_t))});
			totalVertexSize += static_cast<uint32_t>(vertexBufferMemReq.size);
			totalIndexSize += static_cast<uint32_t>(indexBufferMemReq.size);
		}
	}
	VertexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[0].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalVertexSize);
	IndexMemory.allocate(device, device.getPhysicalDevice().findMemoryType(memReqs[1].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), totalIndexSize);
	size_t submeshIdx = 0;
	for(const auto& m : meshes) {
		for(const auto& sm : m.SubMeshes) {
			vkBindBufferMemory(device, sm.getVertexBuffer(), VertexMemory, NextVertexMemoryOffset);
			NextVertexMemoryOffset += memReqs[2 * submeshIdx].size;
			vkBindBufferMemory(device, sm.getIndexBuffer(), IndexMemory, NextIndexMemoryOffset);
			NextIndexMemoryOffset += memReqs[2 * submeshIdx + 1].size;
			++submeshIdx;
		}
	}
	// Create views to the entire dataset
	VertexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalVertexSize);
	vkBindBufferMemory(device, VertexBuffer, VertexMemory, 0);
	IndexBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, totalIndexSize);
	vkBindBufferMemory(device, IndexBuffer, IndexMemory, 0);

	OffsetTableSize = static_cast<uint32_t>(sizeof(OffsetEntry) * offsetTable.size());
	OffsetTableBuffer.create(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, OffsetTableSize);
	OffsetTableMemory.allocate(device, OffsetTableBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	// FIXME: Remove VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT and use a staging buffer.
	OffsetTableMemory.fill(offsetTable.data(), offsetTable.size());
}
