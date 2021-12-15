#version 460

#extension GL_KHR_vulkan_glsl : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputNormalMaterial;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput inputAlbedo;

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

void main() {
   color = subpassLoad(inputAlbedo);
}