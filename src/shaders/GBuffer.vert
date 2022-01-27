#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
	uint frameIndex;
} ubo;

layout(push_constant) uniform constants
{
	mat4 model;
    float metalnessFactor;
    float roughnessFactor;
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
layout(location = 5) out float metalnessFactor;
layout(location = 6) out float roughnessFactor;
layout(location = 7) out flat vec3 origin;

void main() {
    vec4 worldPosition = PushConstants.model * vec4(inPosition, 1.0);
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    position = worldPosition.xyz;
    normal = transpose(inverse(mat3(PushConstants.model))) * inNormal; // transpose(inverse()) is only important in case of non-uniform transformation
    tangent = vec4(mat3(PushConstants.model) * inTangent.xyz, inTangent.w);
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
    metalnessFactor = PushConstants.metalnessFactor;
    roughnessFactor = PushConstants.roughnessFactor;
    origin = (inverse(ubo.view) * vec4(0,0,0,1)).xyz;
}