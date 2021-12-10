#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "irradiance.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;
layout(binding = 1) uniform UBOBlock {
    ProbeGrid grid;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inTexCoord;

layout(location = 0) out vec3 color;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec4 tangent;
layout(location = 3) out vec3 bitangent;
layout(location = 4) out vec2 texCoord;

void main() {
    vec3 gridCellSize = (grid.extentMax - grid.extentMin) / grid.resolution;
    vec3 probePosition = probeIndexToWorldPosition(gl_InstanceIndex, grid);
    gl_Position = ubo.proj * ubo.view * vec4(inPosition + probePosition, 1.0);
    color = inColor;
    normal = inNormal;
    tangent = inTangent;
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
}