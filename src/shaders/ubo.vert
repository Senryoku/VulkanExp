#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
	mat4 model;
} PushConstants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec4 tangent;
layout(location = 3) out vec3 bitangent;
layout(location = 4) out vec2 texCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * PushConstants.model * vec4(inPosition, 1.0);
    position = (PushConstants.model * vec4(inPosition, 1.0)).xyz;
    normal = vec3(PushConstants.model * vec4(inNormal, 1.0));
    tangent = vec4(vec3(PushConstants.model * vec4(inTangent.xyz, 1.0)), inTangent.w);
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
}