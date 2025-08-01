#pragma once

#include <array>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color{1.0f};
	glm::vec3 normal;
	glm::vec4 tangent;
	glm::vec2 texCoord;
	uint32_t  padding = 0; // Align to 4 vec4

	static constexpr VkVertexInputBindingDescription getBindingDescription() {
		VkVertexInputBindingDescription bindingDescription{
			bindingDescription.binding = 0,
			bindingDescription.stride = sizeof(Vertex),
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		return bindingDescription;
	}
	static const std::array<VkVertexInputAttributeDescription, 5>& getAttributeDescriptions() {
		const static std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{
			VkVertexInputAttributeDescription{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, pos),
			},
			VkVertexInputAttributeDescription{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color),
			},
			VkVertexInputAttributeDescription{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal),
			},
			VkVertexInputAttributeDescription{
				.location = 3,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.offset = offsetof(Vertex, tangent),
			},
			VkVertexInputAttributeDescription{
				.location = 4,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, texCoord),
			},
		};
		return attributeDescriptions;
	}
};
