#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "ProbeGrid.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
	uint frameIndex;
} ubo;
layout(binding = 1) uniform UBOBlock {
    ProbeGrid grid;
};
layout(binding = 2, set = 0) buffer ProbesBlock { uint Probes[]; };

#include "irradiance.glsl"

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
layout(location = 5) out ivec2 probeUVOffset;
layout(location = 6) out ivec2 probeDepthUVOffset;
layout(location = 7) out vec2 uvScaling;
layout(location = 8) out uint state;
layout(location = 9) out float gridCellLength;

vec3 gridCellSize = abs((grid.extentMax - grid.extentMin) / grid.resolution);
float ProbeSize = 0.2 * min(gridCellSize.x, min(gridCellSize.y, gridCellSize.z));

void main() {
    vec3 probePosition = probeIndexToWorldPosition(gl_InstanceIndex, grid);
    uvScaling = vec2(grid.resolution.x * grid.resolution.y, grid.resolution.z);
    probeUVOffset = probeIndexToColorUVOffset(probeLinearIndexToGridIndex(gl_InstanceIndex, grid), grid);
    probeDepthUVOffset = probeIndexToDepthUVOffset(probeLinearIndexToGridIndex(gl_InstanceIndex, grid), grid);
    gl_Position = ubo.proj * ubo.view * vec4(ProbeSize * inPosition + probePosition, 1.0);
    color = inColor;
    normal = inNormal;
    tangent = inTangent;
    bitangent = cross(normal, tangent.xyz) * inTangent.w;
    texCoord = inTexCoord;
    state = Probes[gl_InstanceIndex];
    gridCellLength = length(gridCellSize);
}