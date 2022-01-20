#ifdef IRRADIANCE
#extension GL_EXT_shader_atomic_float : enable
layout (local_size_x = 1, local_size_y = 6, local_size_z = 6) in;
#else
layout (local_size_x = 1, local_size_y = 14, local_size_z = 14) in;
#endif

#include "ProbeGrid.glsl"

layout(binding = 6, set = 0, std140) uniform Block {
    ProbeGrid grid;
};
layout(binding = 7, set = 0) buffer ProbesBlock { uint Probes[]; };
layout(set = 0, binding = 11, rgba32f) uniform image2D rayIrradianceDepth;
layout(set = 0, binding = 12, rgba32f) uniform image2D rayDirection;
#ifdef IRRADIANCE
layout(set = 0, binding = 13, r11f_g11f_b10f) uniform image2D imageOut;
#else
layout(set = 0, binding = 14, rg16f) uniform image2D imageOut;
#endif
layout(set = 0, binding = 15) buffer ProbeIndicesBlock { uint indices[]; };

layout(push_constant) uniform Push {
    mat4 randomOrientation;
} push;

#include "irradiance.glsl"

float max3 (vec3 v) {
  return max (max (v.x, v.y), v.z);
}

#ifdef IRRADIANCE
// For res = 6
vec2 specializedNormalizeLocalTexelCoord(ivec2 coord) {
    return 3.33333 * coord + 0.833333;
}
#else
// For res = 14
vec2 specializedNormalizeLocalTexelCoord(ivec2 coord) {
    return 0.142857 * coord - 0.928571;
}
#endif

shared float globalMaxChange;

void main()
{
    uint outOfRange = 0;

    uint linearIndex = indices[gl_WorkGroupID.x];
    ivec3 probeIndex = probeLinearIndexToGridIndex(linearIndex, grid);
    ivec2 localFragCoord = ivec2(gl_LocalInvocationID.yz);

    float gridCellSize = max3((grid.extentMax - grid.extentMin) / grid.resolution);

    vec4 result = vec4(0);
    vec3 texelDirection = octDecode(specializedNormalizeLocalTexelCoord(localFragCoord));
    bool missed = true;
    for(int i = 0; i < grid.raysPerProbe; ++i) {
        vec4 rayData = imageLoad(rayIrradianceDepth, ivec2(gl_GlobalInvocationID.x, i));
        vec3 direction = imageLoad(rayDirection, ivec2(0, i)).xyz;
#ifdef IRRADIANCE
        if(rayData.w < 0 || rayData.w > gridCellSize) ++outOfRange;
        float weight = max(0.0, dot(texelDirection, direction));
        result += vec4(weight * rayData.rgb, weight);
#else
        float weight = pow(max(0.0, dot(texelDirection, direction)), grid.depthSharpness);
        result += vec4(weight * rayData.w, weight * rayData.w * rayData.w, 0.0, weight);
#endif
    }

    if(result.w > 1e-3)
        result.rgb /= result.w;
#ifdef IRRADIANCE
    ivec2 globalFragCoords = probeIndexToColorUVOffset(probeIndex, grid) + ivec2(1, 1) + localFragCoord;
#else
    ivec2 globalFragCoords = probeIndexToDepthUVOffset(probeIndex, grid) + ivec2(1, 1) + localFragCoord;
#endif
    vec3 previous = imageLoad(imageOut, globalFragCoords).rgb;
    float hysteresis = grid.hysteresis;
#ifdef IRRADIANCE
    // Adjust hysteresis if enough changes are detected for faster convergence.
    float maxChange = max3(abs(result.rgb - previous.rgb));
    if(maxChange > 0.25) hysteresis = max(0, hysteresis - 0.10);
    if(maxChange > 0.80) hysteresis = 0.0;
#endif
    result.rgb = mix(result.rgb, previous, hysteresis);
    imageStore(imageOut, globalFragCoords, vec4(result.rgb, 1.0));
#ifdef IRRADIANCE
    atomicAdd(globalMaxChange, maxChange); // FIXME: This should be an atomicMax, but GL_EXT_shader_atomic_float isn't supported by my GPU/driver :(
    if(localFragCoord == ivec2(0)) {
        if(outOfRange >= grid.raysPerProbe) { // Force to a low refresh-rate when we hit no geometry close enough to be shaded by this probe.
            Probes[linearIndex] = 8;
        } else {
            groupMemoryBarrier();
            globalMaxChange /= 6 * 6;
            if(globalMaxChange < 0.02 / Probes[linearIndex]) Probes[linearIndex] = min(Probes[linearIndex] + 1, 8);
            if(globalMaxChange > 0.04 / Probes[linearIndex]) Probes[linearIndex] = max(Probes[linearIndex] - 1, 1);
            if(globalMaxChange > 0.25) Probes[linearIndex] = 1;
        }
    }
#endif
}