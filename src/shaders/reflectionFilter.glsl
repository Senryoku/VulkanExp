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
layout(set = 0, binding = 1, rgba32f) uniform image2D motionVectorsTex;
layout(set = 0, binding = 2, rgba32f) uniform image2D inImage;
layout(set = 0, binding = 3, rgba32f) uniform image2D outImage;
layout(set = 0, binding = 4) uniform PrevUBOBlock
{
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} prevUBO;
layout(set = 0, binding = 5, rgba32f) uniform image2D prevReflection;
layout(set = 0, binding = 6) uniform UBOBlock
{
    mat4 view;
    mat4 proj;
    vec3 origin;
	uint frameIndex;
} ubo;

//#define DISABLE

#define EDGE_PRESERVING

float gaussian(float stdDev, float dist) {
    return (1 / (sqrt(2 * 3.14159) * stdDev)) * exp(-(dist * dist) / (2 * stdDev * stdDev));
}

const float maxDev = 5.0;               // FIXME: This is arbitrary.
const uint  iMaxDev = uint(maxDev + 1);
const float depthFactor = 1.0 / 20.0;   // FIXME: This is arbitrary.
const float baseHysteresis = 0.98;
const float depthStdDev = 0.1;          // FIXME: Also arbitrary.

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
    vec4 positionDepth = getPositionDepth(localID);
    vec3 position = positionDepth.xyz;
    float depth = positionDepth.w;
    float roughness = imageLoad(inImage, coords).w;
    float stdDev = max(0, maxDev * roughness / max(1, (depthFactor * depth))); 
    if(stdDev == 0) { // Skip filter entirely if roughness == 0 since we only need a single sample anyway.
        imageStore(outImage, coords, imageLoad(inImage, coords)); 
        return;
    }
    const float sqrDev = stdDev * stdDev;
    const int window = int(clamp(ceil(sqrt(-2 * sqrDev * log(0.01 * stdDev * sqrt(2 * 3.14159)))), 1, maxDev));
    float totalFactor = 0;

    int minOffset = -min(window, coords[DIR]);
    int maxOffset = min(window, launchSize[DIR] - coords[DIR]);

    // TODO: Base the kernel on the roughness. A better approximation should use as much available geometric data as possible (sample direction, depth...)
    for(int i = minOffset; i <= maxOffset; ++i) {
        const int offset = localID + i;
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
        imageStore(outImage, coords, vec4(final.rgb, roughness)); // This will be used as input in the next pass, also store the roughness.
#else
		// Re-project position to previous frame pixel coords. (Only considering camera motion since we don't have motion vectors yet (and no dynamic geometry anyway :D))
		// TODO: Reprojecting reflections is actually harder than this :( See: http://bitsquid.blogspot.com/2017/06/reprojecting-reflections_22.html
		// This necessitates an additionnal buffer of reflection position (if I understood correctly!) and reflection motion vector (if we actually add them someday).
        // It breaks really bad when the camera is moving (as in 'translating', not just rotating), we could also simply detect this case and reduce hysteresis as a workaround.
		float hysteresis = baseHysteresis;
		vec4 previousValue = vec4(0);
        float cameraMovement = length(ubo.origin - prevUBO.origin);
        hysteresis *= max(0, 1.0 - cameraMovement);
        if(hysteresis > 0) {
            vec4 motionVector = imageLoad(motionVectorsTex, coords);
		    vec4 prevCoords = (prevUBO.proj * (prevUBO.view * vec4(position - motionVector.xyz, 1.0)));
		    prevCoords.xy /= prevCoords.w;
		    prevCoords.xy = (0.5 * prevCoords.xy + 0.5) * launchSize;
		    // TODO: Discard if mismatching (using previous position/depth? instanceID?)
		    if(prevCoords.x > launchSize.x || prevCoords.x < 0 || prevCoords.y > launchSize.y || prevCoords.y < 0) // Discard history if out-of-bounds
			    hysteresis = 0.0f;
		    else {
			    previousValue = imageLoad(prevReflection, ivec2(prevCoords.xy));
                // Discard history if the previous position is too different from the current one (i.e. the pixel probably doesn't map to the same object anymore).
                // Reconstruct previous position from its (linear, world space) depth and the previous ubo.
                vec3 previousPosition = prevUBO.origin + previousValue.w * normalize(position - motionVector.xyz - prevUBO.origin);
                float diff = length(position - previousPosition);
                if(diff < 0.01) diff = 0; // Clip differences that could be accounted to some 'small' precisions errors (especially if the scene is huge), this factor is scene dependent.
                float factor = length(position - previousPosition);
                hysteresis *= 1.0 - clamp(factor, 0, 1);
            }
        }
	    imageStore(outImage, coords, vec4(mix(final.rgb, previousValue.rgb, hysteresis), depth));
#endif
}