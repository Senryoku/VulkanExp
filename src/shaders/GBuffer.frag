#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;
layout(set = 0, binding = 2) uniform sampler2D normalTexSampler;
layout(set = 0, binding = 3) uniform sampler2D metalRoughTexSampler;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexSampler;

layout(set = 0, binding = 5) readonly buffer MaterialsBlock { uint Materials[]; };

#include "unpackMaterial.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangent;
layout(location = 4) in vec3 bitangent;
layout(location = 5) in vec2 texCoord;
layout(location = 6) in vec3 motion;

layout(location = 0) out vec4 outPositionDepth;
layout(location = 1) out vec4 outNormalMetalness;
layout(location = 2) out vec4 outAlbedoRoughness;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec4 outMotion;

void main() {
    Material material = unpackMaterial(0);
    vec3 albedo = color * material.baseColorFactor;
    if(material.albedoTexture != -1) {
        vec4 texColor = texture(texSampler, texCoord);
        if(texColor.a < 0.05) discard; // FIXME: This is a really bad way of handling transparency :)
        albedo *= texColor.rgb;
    }

    vec3 finalNormal = normalize(normal);
    if(material.normalTexture != -1) {
        vec3 normalMap = texture(normalTexSampler, texCoord).rgb;
        vec3 tangentSpaceNormal = normalize(2.0 * normalMap - 1.0);
        finalNormal = normalize(mat3(normalize(tangent.xyz), normalize(bitangent), finalNormal) * tangentSpaceNormal);
    }
    
    float metalness = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if(material.metallicRoughnessTexture != -1) {
        vec4 metalRoughMap = texture(metalRoughTexSampler, texCoord);
        roughness *= metalRoughMap.g;
        metalness *= metalRoughMap.b;
    }

    vec3 emissive = material.emissiveFactor;
    if(material.emissiveTexture != -1) {
        vec4 emissiveTex = texture(emissiveTexSampler, texCoord);
        emissive *= emissiveTex.rgb;
    }

    outPositionDepth = vec4(position, length(position - ubo.origin.xyz));
    outNormalMetalness = vec4(finalNormal, metalness);
    outAlbedoRoughness = vec4(albedo, roughness);
    outEmissive = vec4(emissive, 1.0);
    outMotion = vec4(motion, 1.0);
}