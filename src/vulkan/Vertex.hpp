#pragma once

#include <array>

#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;

    static constexpr VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{
            bindingDescription.binding = 0,
            bindingDescription.stride = sizeof(Vertex),
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        return bindingDescription;
    }
    static constexpr std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{
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
        };

        return attributeDescriptions;
    }
};