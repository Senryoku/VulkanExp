#include "Application.hpp"

void Application::initImGui(uint32_t queueFamily) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
    // io.ConfigViewportsNoAutoMerge = true;
    // io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Create Dear ImGUI Descriptor Pool
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };
    VkDescriptorPoolCreateInfo pool_info = { 
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1000 * IM_ARRAYSIZE(pool_sizes),
        .poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes)),
        .pPoolSizes = pool_sizes,
    };
    if(vkCreateDescriptorPool(_device, &pool_info, nullptr, &_imguiDescriptorPool))
        throw std::runtime_error("Failed to create dear imgui descriptor pool.");

    ImGui_ImplGlfw_InitForVulkan(_window, true);
    ImGui_ImplVulkan_InitInfo init_info = {
        .Instance = _instance,
        .PhysicalDevice = _physicalDevice,
        .Device = _device,
        .QueueFamily = queueFamily,
        .Queue = _graphicsQueue,
        .PipelineCache = VK_NULL_HANDLE,
        .DescriptorPool = _imguiDescriptorPool,
        .MinImageCount = 2,
        .ImageCount = static_cast<uint32_t>(_swapChainImages.size()),
        .Allocator = VK_NULL_HANDLE,
        .CheckVkResultFn = nullptr,
    };
    ImGui_ImplVulkan_Init(&init_info, _imguiRenderPass);

    immediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void Application::drawFrame() {
    VkFence currentFence = _inFlightFences[_currentFrame];
    vkWaitForFences(_device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    auto result = vkAcquireNextImageKHR(_device, _swapChain, UINT64_MAX, _imageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if(result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error(fmt::format("Failed to acquire swap chain image. (Error: {})", result));
    }

    // Check if a previous frame is using this image (i.e. there is its
    // fence to wait on)
    if(_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(_device, 1, &_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    // Mark the image as now being in use by this frame
    _imagesInFlight[imageIndex] = currentFence;

    updateUniformBuffer(imageIndex);

    VkSemaphore waitSemaphores[] = {_imageAvailableSemaphore[_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {_renderFinishedSemaphore[_currentFrame]};
    auto commandBuffer = _commandBuffers.getBuffers()[imageIndex].getHandle();

    // Dear IMGUI
    auto imguiCmdBuff = _imguiCommandBuffers.getBuffers()[imageIndex].getHandle();
    if(vkResetCommandPool(_device, _imguiCommandPool, 0) != VK_SUCCESS)
        throw std::runtime_error("vkResetCommandPool error");
    VkCommandBufferBeginInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(vkBeginCommandBuffer(imguiCmdBuff, &info) != VK_SUCCESS)
        throw std::runtime_error("vkBeginCommandBuffer error");
    std::array<VkClearValue, 1> clearValues{
        VkClearValue{.color = {0.0f, 0.0f, 0.0f, 1.0f}},
    };
    VkRenderPassBeginInfo rpinfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = _imguiRenderPass,
        .framebuffer = _imguiFramebuffers[imageIndex],
        .renderArea = {.extent = _swapChainExtent},
        .clearValueCount = 1,
        .pClearValues = clearValues.data(),
    };
    vkCmdBeginRenderPass(imguiCmdBuff, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), imguiCmdBuff);
    vkCmdEndRenderPass(imguiCmdBuff);
    vkEndCommandBuffer(imguiCmdBuff);

    VkCommandBuffer cmdbuff[2]{commandBuffer, imguiCmdBuff};

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 2,
        .pCommandBuffers = cmdbuff,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores,
    };

    vkResetFences(_device, 1, &currentFence);
    if(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, currentFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkSwapchainKHR swapChains[] = {_swapChain};
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = swapChains,
        .pImageIndices = &imageIndex,
        .pResults = nullptr // Optional
    };
    result = vkQueuePresentKHR(_presentQueue, &presentInfo);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized) {
        _framebufferResized = false;
        recreateSwapChain();
    } else if(result != VK_SUCCESS) {
        throw std::runtime_error(fmt::format("Failed to present swap chain image. (Error: {})", result));
    }

    _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}