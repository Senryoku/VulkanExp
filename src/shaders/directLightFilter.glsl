#define WORKGROUP_SIZE 64

#ifdef DIRECTION_X
layout (local_size_x = WORKGROUP_SIZE, local_size_y = 1) in;
#define DIR 0
const ivec2 offsetDirection = ivec2(1, 0);
#else
layout (local_size_x = 1, local_size_y = WORKGROUP_SIZE) in;
#define DIR 1
const ivec2 offsetDirection = ivec2(0, 1);
#endif

layout(set = 0, binding = 0, rgba32f) uniform image2D positionDepthTex;
layout(set = 0, binding = 1, rgba32f) uniform image2D inImage;
layout(set = 0, binding = 2, rgba32f) uniform image2D outImage;
layout(set = 0, binding = 3) uniform PrevUBOBlock
{
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} prevUBO;
layout(set = 0, binding = 4, rgba32f) uniform image2D prevDirectLight;

//#define DISABLE

#define EDGE_PRESERVING

float gaussian(float stdDev, float dist) {
    return (1 / (sqrt(2 * 3.14159) * stdDev)) * exp(-(dist * dist) / (2 * stdDev * stdDev));
}

const float maxDev = 7.0;                    // FIXME: This is arbitrary.
const uint  iMaxDev = uint(maxDev + 1);
const float depthFactor = 1.0 / 0.5;         // FIXME: This is arbitrary.
const float baseHysteresis = 0.94;
const float depthStdDev = 0.01;              // FIXME: Also arbitrary.
const float historyDistanceThreshold = 0.05; // Invalidate history when the reprojecting is off by more than this threshold. (Also dependant on the scene, so... FIXME: Pass as uniform.)

// FIXME: Surfaces close to the camera are black (related to the depth used in stdDev)
// TODO: Use Depth from the Occlusion pass (not only the gbuffer) to drive the stdDev, or, even better, the variance (from a history buffer)? (similar to roughness usage in reflections).

shared vec4 positionDepthTexCache[WORKGROUP_SIZE + 2 * iMaxDev];
vec4 getPositionDepth(int localCoord) {
    return positionDepthTexCache[uint(localCoord + iMaxDev)];
}

shared vec4 inImageTexCache[WORKGROUP_SIZE + 2 * iMaxDev];
vec4 getInImage(int localCoord) {
    return inImageTexCache[uint(localCoord + iMaxDev)];
}

void main()
{
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    
#ifdef DISABLE
    imageStore(outImage, coords, imageLoad(inImage, coords));
    return;
#endif

    const ivec2 launchSize = imageSize(inImage);
    const int localID = int(gl_LocalInvocationID[DIR]);

    // Preload neighbor positionDepthTex
    positionDepthTexCache[iMaxDev + localID] = imageLoad(positionDepthTex, coords);
    inImageTexCache      [iMaxDev + localID] = imageLoad(inImage,          coords);
    if(localID < iMaxDev) {
        positionDepthTexCache[localID]               = imageLoad(positionDepthTex, coords - ivec2(iMaxDev * offsetDirection));
        inImageTexCache      [localID]               = imageLoad(inImage,          coords - ivec2(iMaxDev * offsetDirection));
    }
    if(localID + iMaxDev > WORKGROUP_SIZE) {
        positionDepthTexCache[2 * iMaxDev + localID] = imageLoad(positionDepthTex, coords + ivec2(iMaxDev * offsetDirection));
        inImageTexCache      [2 * iMaxDev + localID] = imageLoad(inImage,          coords + ivec2(iMaxDev * offsetDirection));
    }
    memoryBarrierShared();

    vec4 final = vec4(0);
    const vec4  positionDepth = getPositionDepth(localID);
    const vec3  position = positionDepth.xyz;
    const float depth = positionDepth.w;
    const float stdDev = 1 + max(1, maxDev / (max(1, depthFactor * depth)));
    const int   window = int(clamp(ceil(sqrt(-2 * stdDev * stdDev * log(0.01 * stdDev * sqrt(2 * 3.14159)))), 1, iMaxDev));
    
    float totalFactor = 0;

    int minOffset = -min(window, coords[DIR]);
    int maxOffset = min(window, launchSize[DIR] - coords[DIR]);

    for(int i = minOffset; i <= maxOffset; ++i) {
        int offset = localID + i;
        float factor = gaussian(stdDev, i);
#ifdef EDGE_PRESERVING
        factor *= gaussian(depthStdDev, abs(depth - getPositionDepth(offset).w));
        totalFactor += factor;
#endif
        final += factor * getInImage(offset);
    }

#ifdef EDGE_PRESERVING
    if(totalFactor > 1e-2)
        final /= totalFactor;
    else final = vec4(0);
#endif

#ifdef DIRECTION_X
    imageStore(outImage, coords, vec4(final.rgb, depth));
#else		
	final.r = clamp(final.r, 0, 1);
    final.g = final.r * final.r;
    // Re-project position to previous frame pixel coords. (Only considering camera motion)
	vec4 prevCoords = (prevUBO.proj * (prevUBO.view * vec4(position, 1.0)));
    vec4 previousRay = vec4(prevCoords.xy, 1, 1);
	prevCoords.xy /= prevCoords.w;
	prevCoords.xy = (0.5 * prevCoords.xy + 0.5) * launchSize;
	vec4 previousValue = vec4(0);
	float hysteresis = baseHysteresis;

    if(final.b > 0) // The blue channel is used to mark a high variance region, spread to the neighbors by the previous filter.
        hysteresis = final.b == 1 ? 0.5 : 0; 

	if(prevCoords.x > launchSize.x || prevCoords.x < 0 || prevCoords.y > launchSize.y || prevCoords.y < 0) // Discard history if out-of-bounds
		hysteresis = 0.0f;
	else {
		previousValue = imageLoad(prevDirectLight, ivec2(prevCoords.xy));
        // Discard history if the previous position is too different from the current one (i.e. the pixel probably doesn't map to the same object anymore).
        // Reconstruct previous position from its (linear, world space) depth and the previous ubo.
        vec3 previousPosition = prevUBO.origin + previousValue.w * normalize(position - prevUBO.origin);
        float diff = length(position - previousPosition);
        if(diff < 0.001) diff = 0; // Clip differences that could be accounted to some 'small' precisions errors (especially if the scene is huge), this factor is scene dependent.
        float factor = clamp(length(position - previousPosition), 0, historyDistanceThreshold) / historyDistanceThreshold;
        hysteresis *= 1.0 - clamp(factor, 0, 1);

        float variance = abs(previousValue.r * previousValue.r - previousValue.g);
        // This region was stable (low variance), but the last sample contradicts it: Invalidate history.
        if(variance < 0.25 && abs(previousValue.r - final.r) > 0.75) {
            hysteresis = 0;
            final.b = 1.0; 
        } else final.b = 0.0;
	}
	imageStore(outImage, coords, vec4(hysteresis * previousValue.rgb + (1.0f - hysteresis) * final.rgb, depth));
#endif
}