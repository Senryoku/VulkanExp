#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} ubo;

#include "InstanceData.glsl"
layout(set = 1, binding = 0) readonly buffer InstanceDataBlock {
    InstanceData instances[];
};
layout(set = 1, binding = 1) readonly buffer PreviousInstanceDataBlock {
    InstanceData previousInstances[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord;
#ifdef SKINNED
layout(location = 5) in vec4 inMotionVector;
#endif

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 normal;
layout(location = 3) out vec4 tangent;
layout(location = 4) out vec3 bitangent;
layout(location = 5) out vec2 texCoord;
layout(location = 6) out vec3 motion;

void main() {
    mat4 model = instances[gl_InstanceIndex].transform;
    vec4 worldPosition = model * vec4(inPosition, 1.0);
    vec4 viewPosition = ubo.view * worldPosition;
    gl_Position = ubo.proj * viewPosition;
    position = worldPosition.xyz;
    normal = transpose(inverse(mat3(model))) * inNormal; // transpose(inverse()) is only important in case of non-uniform transformation
    tangent = vec4(mat3(model) * inTangent.xyz, inTangent.w);
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
    color = inColor;
    motion = (worldPosition - previousInstances[gl_InstanceIndex].transform * vec4(inPosition, 1.0)).xyz;
#ifdef SKINNED
    motion += mat3(model) * inMotionVector.xyz;
#endif
}