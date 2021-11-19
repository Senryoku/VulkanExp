#include <Application.hpp>

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for(VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if(tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if(tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format.");
}

void Application::createSwapChain() {
    auto swapChainSupport = _physicalDevice.getSwapChainSupport(_surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if(swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = _surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, // VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ?
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    auto indices = _physicalDevice.getQueues(_surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if(indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;     // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    if(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain.");
    }

    vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, _swapChainImages.data());

    _swapChainImageFormat = surfaceFormat.format;
    _swapChainExtent = extent;

    for(size_t i = 0; i < _swapChainImages.size(); i++)
        _swapChainImageViews.push_back(ImageView{_device, _swapChainImages[i], _swapChainImageFormat});

    _depthFormat = findSupportedFormat(_physicalDevice, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
                                       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    _depthImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, _depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto memRequirements = _depthImage.getMemoryRequirements();
    _depthImageMemory.allocate(_device, _physicalDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), memRequirements.size);
    vkBindImageMemory(_device, _depthImage, _depthImageMemory, 0);
    _depthImageView.create(_device, _depthImage, _depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Application::initSwapChain() {
    _renderPass.create(_device, _swapChainImageFormat, _depthFormat);

    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    _imguiRenderPass.create(_device, 
        std::array < VkAttachmentDescription, 1>{VkAttachmentDescription{
                                .format = _swapChainImageFormat,
                                .samples = VK_SAMPLE_COUNT_1_BIT,
                                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            }},
                            std::array<VkSubpassDescription, 1>{VkSubpassDescription{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
        }},
                            std::array<VkSubpassDependency, 1>{VkSubpassDependency{
                                .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        }}
    );

    Shader vertShader(_device, "./shaders_spv/ubo.vert.spv");
    Shader fragShader(_device, "./shaders_spv/phong.frag.spv");
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
        vertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        fragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
    };
    _descriptorSetLayout.create(_device);
    _pipeline.create(_device, shaderStages, _renderPass, _swapChainExtent, {_descriptorSetLayout});

    _swapChainFramebuffers.resize(_swapChainImageViews.size());
    for(size_t i = 0; i < _swapChainImageViews.size(); i++)
        _swapChainFramebuffers[i].create(_device, _renderPass, {_swapChainImageViews[i], _depthImageView}, _swapChainExtent);

    _imguiFramebuffers.resize(_swapChainImageViews.size());
    for(size_t i = 0; i < _swapChainImageViews.size(); i++) {
        _imguiFramebuffers[i].create(_device, _imguiRenderPass, _swapChainImageViews[i], _swapChainExtent);
    }

    _commandBuffers.allocate(_device, _commandPool, _swapChainFramebuffers.size());
    _imguiCommandBuffers.allocate(_device, _imguiCommandPool, _swapChainFramebuffers.size());

    _imagesInFlight.resize(_swapChainImages.size());

    // Uniform buffers init
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        _uniformBuffers.resize(_swapChainImages.size());

        for(size_t i = 0; i < _swapChainImages.size(); i++)
            _uniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);

        auto memReq = _uniformBuffers[0].getMemoryRequirements();
        size_t memSize = (2 + _swapChainImages.size() * bufferSize / memReq.alignment) * memReq.alignment;
        _uniformBuffersMemory.allocate(_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                                       memSize);
        size_t offset = 0;
        for(size_t i = 0; i < _swapChainImages.size(); i++) {
            vkBindBufferMemory(_device, _uniformBuffers[i], _uniformBuffersMemory, offset);
            offset += bufferSize;
            offset = (1 + offset / memReq.alignment) * memReq.alignment;
        }
    }

    _descriptorPool.create(_device, _swapChainImages.size());
    _descriptorPool.allocate(_swapChainImages.size(), _descriptorSetLayout);
    ;
    for(size_t i = 0; i < _swapChainImages.size(); i++) {
        VkDescriptorBufferInfo bufferInfo{
            .buffer = _uniformBuffers[i],
            .offset = 0,
            .range = sizeof(UniformBufferObject),
        };

        VkWriteDescriptorSet descriptorWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = _descriptorPool.getDescriptorSets()[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr, // Optional
            .pBufferInfo = &bufferInfo,
            .pTexelBufferView = nullptr, // Optional
        };

        vkUpdateDescriptorSets(_device, 1, &descriptorWrite, 0, nullptr);
    }

    for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
        auto b = _commandBuffers.getBuffers()[i];
        b.begin();
        b.beginRenderPass(_renderPass, _swapChainFramebuffers[i], _swapChainExtent);
        _pipeline.bind(b);

        _commandBuffers[i].bind<1>({_mesh.getVertexBuffer()});
        vkCmdBindIndexBuffer(_commandBuffers[i], _mesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.getLayout(), 0, 1, &_descriptorPool.getDescriptorSets()[i], 0, nullptr);
        vkCmdDrawIndexed(_commandBuffers[i], static_cast<uint32_t>(_mesh.getIndices().size()), 1, 0, 0, 0);

        b.endRenderPass();
        b.end();
    }
}

void Application::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(_window, &width, &height);
    while(width == 0 || height == 0) {
        glfwGetFramebufferSize(_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(_device);

    cleanupSwapChain();

    createSwapChain();
    initSwapChain();
}

void Application::cleanupSwapChain() {
    for(auto& b : _uniformBuffers)
        b.destroy();
    _uniformBuffersMemory.free();
    _descriptorPool.destroy();
    _swapChainFramebuffers.clear();
    _imguiFramebuffers.clear();

    // Only free up the command buffer, not the command pool
    _commandBuffers.free();
    _imguiCommandBuffers.free();
    _pipeline.destroy();
    _descriptorSetLayout.destroy();
    _renderPass.destroy();
    _imguiRenderPass.destroy();
    _depthImageView.destroy();
    _depthImage.destroy();
    _depthImageMemory.free();
    _swapChainImageViews.clear();
    vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}