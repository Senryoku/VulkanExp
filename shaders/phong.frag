#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 outColor;

vec3 lightDir = normalize(vec3(-1/sqrt(3), 0, -1/sqrt(3)));

void main() {
    outColor = clamp(dot(lightDir, normal), 0.05, 1.0) * vec4(fragColor, 1.0);
}