#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "irradiance.glsl"

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D normalTexSampler;

layout(location = 0) in vec4 positionDepth;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;
layout(location = 5) in flat uint material;

layout(location = 0) out vec4 outPositionDepth;
layout(location = 1) out vec4 outNormalMaterial;
layout(location = 2) out vec4 outAlbedo; // Could pack something else here?

vec3 lightDir = normalize(vec3(-1, 6, 1));

void main() {
    vec4 texColor = texture(texSampler, texCoord);
    if(texColor.a < 0.05) discard; // FIXME: This is a really bad way of handling transparency :)

    vec3 tangentSpaceNormal = normalize(2.0 * texture(normalTexSampler, texCoord).rgb - 1.0);
    vec3 finalNormal = mat3(tangent.xyz, bitangent, normalize(normal)) * tangentSpaceNormal;

    outPositionDepth = positionDepth;
    outNormalMaterial = vec4(finalNormal, uintBitsToFloat(material));
    outAlbedo = texColor;
}