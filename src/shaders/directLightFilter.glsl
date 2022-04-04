layout (local_size_x = 32, local_size_y = 32) in;

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
layout(set = 0, binding = 4, rgba32f) uniform image2D prevReflection;

//#define DISABLE

#define EDGE_PRESERVING

float gaussian(float stdDev, float dist) {
    return (1 / (sqrt(2 * 3.14159) * stdDev)) * exp(-(dist * dist) / (2 * stdDev * stdDev));
}

const float maxDev = 7.0;                    // FIXME: This is arbitrary.
const float depthFactor = 1.0 / 0.5;         // FIXME: This is arbitrary.
const float baseHysteresis = 0.94;
const float depthStdDev = 0.01;              // FIXME: Also arbitrary.
const float historyDistanceThreshold = 0.05; // Invalidate history when the reprojecting is off by more than this threshold. (Also dependant on the scene, so... FIXME: Pass as uniform.)

// FIXME: Surfaces close to the camera are black (related to the depth used in stdDev)
// TODO: Use Depth from the Occlusion pass (not only the gbuffer) to drive the stdDev, or, even better, the variance (from a history buffer)? (similar to roughness usage in reflections).

void main()
{
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
#ifdef DISABLE
    imageStore(outImage, coords, imageLoad(inImage, coords));
    return;
#endif
    vec4 final = vec4(0);
    vec4 positionDepth = imageLoad(positionDepthTex, coords);
    vec3 position = positionDepth.xyz;
    float depth = positionDepth.w;
    float stdDev = 1 + max(1, maxDev / (max(1, depthFactor * depth)));
    int window = int(clamp(ceil(sqrt(-2 * stdDev * stdDev * log(0.01 * stdDev * sqrt(2 * 3.14159)))), 1, maxDev + 1));
    
    float totalFactor = 0;
    ivec2 launchSize = imageSize(inImage);
#ifdef DIRECTION_X
    int minOffset = -min(window, coords.x);
    int maxOffset = min(window, launchSize.x - coords.x);
#else
    int minOffset = -min(window, coords.y);
    int maxOffset = min(window, launchSize.y - coords.y);
#endif
    for(int i = minOffset; i <= maxOffset; ++i) {
#ifdef DIRECTION_X
        ivec2 offset = ivec2(i, 0);
#else
        ivec2 offset = ivec2(0, i);
#endif
#ifdef EDGE_PRESERVING
        float factor = gaussian(stdDev, i) * gaussian(depthStdDev, abs(depth - imageLoad(positionDepthTex, coords + offset).w));
        totalFactor += factor;
#else
        float factor = gaussian(stdDev, i);
#endif
        final += factor * imageLoad(inImage, coords + offset);
    }
#ifdef EDGE_PRESERVING
    if(totalFactor > 1e-2)
        final /= totalFactor;
    else final = vec4(0);
#endif
#ifdef DIRECTION_X
        imageStore(outImage, coords, vec4(final.rgb, depth));
#else		
    // Re-project position to previous frame pixel coords. (Only considering camera motion since we don't have motion vectors yet (and no dynamic geometry anyway :D))
	vec4 prevCoords = (prevUBO.proj * (prevUBO.view * vec4(position, 1.0)));
    vec4 previousRay = vec4(prevCoords.xy, 1, 1);
	prevCoords.xy /= prevCoords.w;
	prevCoords.xy = (0.5 * prevCoords.xy + 0.5) * launchSize;
	vec4 previousValue = vec4(0);
	float hysteresis = baseHysteresis;
	if(prevCoords.x > launchSize.x || prevCoords.x < 0 || prevCoords.y > launchSize.y || prevCoords.y < 0) // Discard history if out-of-bounds
		hysteresis = 0.0f;
	else {
		previousValue = imageLoad(prevReflection, ivec2(prevCoords.xy));
        // Discard history if the previous position is too different from the current one (i.e. the pixel probably doesn't map to the same object anymore).
        vec3 previousOrigin = (inverse(prevUBO.view) * vec4(0, 0, 0, 1)).xyz;
        // Reconstruct previous position from its (linear, world space) depth and the previous ubo.
        vec3 previousPosition = previousOrigin + previousValue.w * normalize(position - previousOrigin);
        float diff = length(position - previousPosition);
        if(diff < 0.001) diff = 0; // Clip differences that could be accounted to some 'small' precisions errors (especially if the scene is huge), this factor is scene dependent.
        float factor = clamp(length(position - previousPosition), 0, historyDistanceThreshold) / historyDistanceThreshold;
        hysteresis *= 1.0 - clamp(factor, 0, 1);
	}
	final.rgb = clamp(final.rgb, 0, 1);
	imageStore(outImage, coords, vec4(hysteresis * previousValue.rgb + (1.0f - hysteresis) * final.rgb, depth));
#endif
}