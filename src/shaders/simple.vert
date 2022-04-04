#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} ubo;

layout(push_constant) uniform constants
{
	mat4 model;
} PushConstants;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * PushConstants.model * vec4(position, 1.0);
    fragColor = color;
}