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

vec2 specializedNormalizeLocalTexelCoord(ivec2 coord) {
    //return (vec2(coords) + vec2(0.5)) * (2.0f / vec2(gl_WorkGroupSize.xy)) - vec2(1.0f);
#ifdef IRRADIANCE
// For res = 6
    return 0.33333 * (coord - 2.5);
#else
// For res = 14
    return 0.142857 * (coord - 6.5);
#endif
}

const uint MaxRaysPerProbe = 256;

shared uint globalMaxChange;
shared vec3	 rayDir[MaxRaysPerProbe];

void main()
{
    uint outOfRange = 0;

    uint linearIndex = indices[gl_WorkGroupID.x];
    ivec3 probeIndex = probeLinearIndexToGridIndex(linearIndex, grid);
    ivec2 localFragCoord = ivec2(gl_LocalInvocationID.yz);

    float gridCellSize = length(probeGridCellSize(grid));

    vec4 result = vec4(0);
    vec3 texelDirection = octDecode(specializedNormalizeLocalTexelCoord(localFragCoord));
	bool missed = true;

	if(gl_LocalInvocationIndex == 0)
		globalMaxChange = floatBitsToUint(0.0);
	if(gl_LocalInvocationIndex < grid.raysPerProbe) 
		rayDir[gl_LocalInvocationIndex] = imageLoad(rayDirection, ivec2(0, gl_LocalInvocationIndex)).xyz;
	memoryBarrierShared();
	barrier();

    for(int i = 0; i < grid.raysPerProbe; ++i) {
        vec4 rayData = imageLoad(rayIrradianceDepth, ivec2(gl_GlobalInvocationID.x, i));
		vec3 direction = rayDir[i];
#ifdef IRRADIANCE
        if(rayData.w < 0 || rayData.w > gridCellSize) ++outOfRange;
        float weight = max(0.0, dot(texelDirection, direction));
        result += vec4(weight * rayData.rgb, weight);
#else
        float depth = min(gridCellSize, rayData.w);
        if(depth < 0) depth = gridCellSize;
        float weight = pow(max(0.0, dot(texelDirection, direction)), grid.depthSharpness);
        result += vec4(weight * depth, weight * depth * depth, 0.0, weight);
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
    float maxChange = max3(abs(result.rgb - previous.rgb));
    // Adjust hysteresis if enough changes are detected for faster convergence.
    // FIXME: These checks allow outliers to have too much impact, resulting in white flashes even if the scene doesn't change.
    //        For now I'd rather have a more stable image.
    //if(maxChange > 0.25) hysteresis = max(0, hysteresis - 0.10);
    //if(maxChange > 0.80) hysteresis = 0.0;
#endif
    result.rgb = mix(result.rgb, previous, hysteresis);
    imageStore(imageOut, globalFragCoords, vec4(result.rgb, 1.0));
#ifdef IRRADIANCE
    // FIXME: This should be a float atomicMax, but GL_EXT_shader_atomic_float2 isn't supported by my GPU/driver :(
	uint asInt = floatBitsToUint(maxChange);
	atomicMax(globalMaxChange, asInt);
	groupMemoryBarrier();

    if(localFragCoord == ivec2(0)) {
        if(outOfRange >= grid.raysPerProbe) { // Force to a low refresh-rate when we hit no geometry close enough to be shaded by this probe.
            Probes[linearIndex] = 8;
        } else {
			maxChange = uintBitsToFloat(globalMaxChange);
            if(maxChange < 0.02 / Probes[linearIndex]) Probes[linearIndex] = min(Probes[linearIndex] + 1, 8);
            else if(maxChange > 0.04 / Probes[linearIndex]) Probes[linearIndex] = max(Probes[linearIndex] - 1, 1);
			else if(maxChange > 0.25) Probes[linearIndex] = 1;
        }
    }
#endif
}