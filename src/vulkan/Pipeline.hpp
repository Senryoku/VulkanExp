#pragma once

#include <stdexcept>
#include <vector>

#include "CommandBuffer.hpp"
#include "HandleWrapper.hpp"
#include "RenderPass.hpp"
#include "Vertex.hpp"

class PipelineLayout : public HandleWrapper<VkPipelineLayout> {
  public:
    void create(VkDevice device, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {}) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),                   // Optional
            .pSetLayouts = descriptorSetLayouts.size() > 0 ? descriptorSetLayouts.data() : nullptr, // Optional
            .pushConstantRangeCount = 0,                                                            // Optional
            .pPushConstantRanges = nullptr,                                                         // Optional
        };
        if(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyPipelineLayout(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
    }

    ~PipelineLayout() {
        destroy();
    }

  private:
    VkDevice _device = VK_NULL_HANDLE;
};

class Pipeline : HandleWrapper<VkPipeline> {
  public:
    void create(VkDevice device, const std::vector<VkPipelineShaderStageCreateInfo>& shaderStages, const RenderPass& renderPass, VkExtent2D swapChainExtent,
                const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {}) {
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription, // Optional
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data(), // Optional
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)swapChainExtent.width,
            .height = (float)swapChainExtent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = swapChainExtent,
        };

        VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f, // Optional
            .depthBiasClamp = 0.0f,          // Optional
            .depthBiasSlopeFactor = 0.0f,    // Optional
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisampling{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,          // Optional
            .pSampleMask = nullptr,            // Optional
            .alphaToCoverageEnable = VK_FALSE, // Optional
            .alphaToOneEnable = VK_FALSE,      // Optional
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,  // Optional
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
            .colorBlendOp = VK_BLEND_OP_ADD,             // Optional
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,  // Optional
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
            .alphaBlendOp = VK_BLEND_OP_ADD,             // Optional
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},            // Optional
            .back = {},             // Optional
            .minDepthBounds = 0.0f, // Optional
            .maxDepthBounds = 1.0f, // Optional
        };

        VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY, // Optional
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f} // Optional
        };

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

        VkPipelineDynamicStateCreateInfo dynamicState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicStates,
        };

        _pipelineLayout.create(device, descriptorSetLayouts);

        VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil, // Optional
            .pColorBlendState = &colorBlending,
            .pDynamicState = nullptr, // Optional
            .layout = _pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE, // Optional
            .basePipelineIndex = -1,              // Optional
        };

        if(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_handle) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create graphics pipeline!");
        }

        _device = device;
    }

    void destroy() {
        if(isValid()) {
            vkDestroyPipeline(_device, _handle, nullptr);
            _handle = VK_NULL_HANDLE;
        }
        _pipelineLayout.destroy();
    }

    void bind(const CommandBuffer& commandBuffer) const {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _handle);
    }

    const PipelineLayout& getLayout() const {
        return _pipelineLayout;
    }

    ~Pipeline() {
        destroy();
    }

  private:
    VkDevice _device;
    PipelineLayout _pipelineLayout;
};