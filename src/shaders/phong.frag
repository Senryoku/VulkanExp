#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "irradiance.glsl"

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D normalTexSampler;
layout(binding = 3) uniform sampler2D probesColor;
layout(binding = 4) uniform sampler2D probesDepth;
layout(binding = 5) uniform UBOBlock {
    ProbeGrid grid;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

vec3 lightDir = normalize(vec3(-1, 6, 1));

void main() {
    vec4 texColor = texture(texSampler, texCoord);
    if(texColor.a == 0) discard; // FIXME: This is a really bad way of handling transparency :)

    vec3 tangentSpaceNormal = normalize(2.0 * texture(normalTexSampler, texCoord).rgb - 1.0);
    vec3 finalNormal = mat3(tangent.xyz, bitangent, normalize(normal)) * tangentSpaceNormal;

    vec3 indirectLight = sampleProbes(position, normalize(normal), grid, probesColor, probesDepth);    

    outColor = vec4(indirectLight * texColor.rgb + clamp(dot(lightDir, finalNormal), 0.2, 1.0) * texColor.rgb, 1.0);

    // DEBUG
    outColor = vec4(indirectLight, 1.0);
    //outColor = vec4(indirectLight * texColor.rgb, 1.0);
}