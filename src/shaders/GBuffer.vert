#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform constants
{
	mat4 model;
    uint material;
} PushConstants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec4 positionDepth;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec4 tangent;
layout(location = 3) out vec3 bitangent;
layout(location = 4) out vec2 texCoord;
layout(location = 5) out flat uint material;

void main() {
    gl_Position = ubo.proj * ubo.view * PushConstants.model * vec4(inPosition, 1.0);
    positionDepth = vec4((PushConstants.model * vec4(inPosition, 1.0)).xyz, gl_Position.z / gl_Position.z);
    normal = mat3(PushConstants.model) * inNormal; // transpose inverse?
    tangent = vec4(mat3(PushConstants.model) * inTangent.xyz, inTangent.w);
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
    material = PushConstants.material;
}