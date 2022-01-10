#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D normalTexSampler;
layout(binding = 3) uniform sampler2D metalRoughTexSampler;
layout(binding = 4) uniform sampler2D emissiveTexSampler;

layout(location = 0) in vec4 positionDepth;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 texCoord;
layout(location = 5) in float metalnessFactor;
layout(location = 6) in float roughnessFactor;

layout(location = 0) out vec4 outPositionDepth;
layout(location = 1) out vec4 outNormalMetalness;
layout(location = 2) out vec4 outAlbedoRoughness;

void main() {
    vec4 texColor = texture(texSampler, texCoord);
    if(texColor.a < 0.05) discard; // FIXME: This is a really bad way of handling transparency :)
    
    vec3 finalNormal = normalize(normal);
    vec3 normalMap = texture(normalTexSampler, texCoord).rgb;
    if(normalMap != vec3(1.0)) { // If this model doesn't have a normal map, it will be replaced with a white one.
        vec3 tangentSpaceNormal = normalize(2.0 * normalMap - 1.0);
        finalNormal = normalize(mat3(normalize(tangent.xyz), normalize(bitangent), finalNormal) * tangentSpaceNormal);
    }
    
    float metalness = metalnessFactor;
    float roughness = roughnessFactor;
    vec4 metalRoughMap = texture(metalRoughTexSampler, texCoord);
    metalness *= metalRoughMap.b;
    roughness *= metalRoughMap.g;

    outPositionDepth = positionDepth;
    outNormalMetalness = vec4(finalNormal, metalness);
    outAlbedoRoughness = vec4(texColor.rgb, roughness);
}