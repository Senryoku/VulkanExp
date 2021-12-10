#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "irradiance.glsl"

layout(binding = 2) uniform sampler2D colorTex;
layout(binding = 3) uniform sampler2D depthTex;

layout(location = 0) in vec3 color;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;
layout(location = 5) flat in ivec2 probeUVOffset;
layout(location = 6) flat in vec2 uvScaling;

layout(location = 0) out vec4 outColor;

const int colorRes = 8; // FIXME

void main() {
    vec2 localUV = (float(colorRes - 2) / colorRes) * spherePointToOctohedralUV(normalize(normal)) / uvScaling;
    vec2 uv = (probeUVOffset  + ivec2(1, 1)) / uvScaling / colorRes + localUV;
    vec3 c = texture(colorTex, uv).xyz;
    outColor = vec4(c, 1.0);
}