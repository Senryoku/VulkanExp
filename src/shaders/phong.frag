#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

vec3 lightDir = normalize(vec3(-1, 1, 1));

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    if(texColor.a == 0) discard; // FIXME: This is a really bad way of handling transparency :)
    outColor = vec4(clamp(dot(lightDir, normal), 0.3, 1.0) * texColor.rgb, 1.0);
}