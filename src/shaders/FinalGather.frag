#version 460

#extension GL_KHR_vulkan_glsl : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputPositionDepth;

layout(location = 0) in vec2 fragPosition;

layout(location = 0) out vec4 color;

void main() {
   color = subpassLoad(inputPositionDepth);
}