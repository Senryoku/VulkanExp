#include "Mesh.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <JSON.hpp>
#include <Logger.hpp>
#include <stringutils.hpp>

void Mesh::init(const Device& device) {
	if(!isValid() && !dynamic)
		return;

	if(_indexBuffer && _vertexBuffer) {
		_indexBuffer.destroy();
		_vertexBuffer.destroy();
	}
	const auto usageBitsForRayTracing = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const auto indexDataSize = getIndexByteSize();
	_indexBuffer.create(device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | usageBitsForRayTracing, indexDataSize);
	const auto vertexDataSize = getVertexByteSize();
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

void Mesh::upload(VkDevice device, const Buffer& stagingBuffer, const DeviceMemory& stagingMemory, const CommandPool& tmpCommandPool, VkQueue queue) {
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

void Mesh::destroy() {
	_indexBuffer.destroy();
	_vertexBuffer.destroy();
	_skinJointsBuffer.destroy();
	_skinWeightsBuffer.destroy();
}

void Mesh::normalizeVertices() {
	glm::vec3 acc{0};
	for(const auto& v : _vertices)
		acc += v.pos;
	acc /= _vertices.size();
	for(auto& v : _vertices)
		v.pos -= acc;
}

void Mesh::computeVertexNormals() {
	// Here, normals are the average of adjacent triangles' normals
	// (so we have exactly one normal per vertex)
	for(auto& v : _vertices) {
		v.normal = glm::vec3{0.0f};
		v.tangent = glm::vec4{0.0f};
	}

	for(size_t i = 0; i < _indices.size();) {
		// Norm of this triangle
		glm::vec3 norm = glm::normalize(glm::cross(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos, _vertices[_indices[i + 2]].pos - _vertices[_indices[i]].pos));
		glm::vec4 tang = glm::vec4{glm::normalize(_vertices[_indices[i]].pos - _vertices[_indices[i + 1]].pos), 1.0};
		// Add it to each vertex
		for(size_t j = 0; j < 3; ++j) {
			_vertices[_indices[i]].normal += norm;
			_vertices[_indices[i]].tangent += tang;
			++i;
		}
	}
	// Average norms
	for(auto& v : _vertices) {
		v.normal = v.normal / v.tangent.w;
		// FIXME: Use UVs when available to properly compute tangents
		v.tangent = glm::vec4{glm::vec3{v.tangent} / v.tangent.w, 1.0f};
	}
}

const Bounds& Mesh::computeBounds() {
	if(!_vertices.empty()) {
		Bounds b{.min = _vertices[0].pos, .max = _vertices[0].pos};
		for(const auto& v : _vertices) {
			b.min = glm::min(b.min, v.pos);
			b.max = glm::max(b.max, v.pos);
		}
		_bounds = b;
	}
	return _bounds;
}
