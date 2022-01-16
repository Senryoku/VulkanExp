layout (local_size_x = 32, local_size_y = 32) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D positionDepth;
layout(set = 0, binding = 1, rgba32f) uniform image2D inImage;
layout(set = 0, binding = 2, rgba32f) uniform image2D outImage;

//#define DISABLE

#define EDGE_PRESERVING

float gaussian(float stdDev, float dist) {
    return (1 / (sqrt(2 * 3.14159) * stdDev)) * exp(-(dist * dist) / (2 * stdDev * stdDev));
}

const float maxDev = 32.0;

// FIXME: Surfaces close to the camera are black (related to the depth used in stdDev)
// TODO: Use Depth from the Occlusion pass (not only the gbuffer) to drive the stdDev? (similar to roughness usage in reflections).

void main()
{
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    vec4 final = vec4(0);
    float depth = imageLoad(positionDepth, coords).w;
    float stdDev = max(1, maxDev / (depth / 10.0)); // FIXME: This is arbitrary.
    float depthStdDev = 1.0;                        // FIXME: Also arbitrary.
    int window = int(clamp(ceil(sqrt(-2 * stdDev * stdDev * log(0.01 * stdDev * sqrt(2 * 3.14159)))), 1, maxDev + 1));
    
    float totalFactor = 0;

    for(int i = -window; i <= window; ++i) {
#ifdef DIRECTION_X
        ivec2 offset = ivec2(i, 0);
#else
        ivec2 offset = ivec2(0, i);
#endif
#ifdef EDGE_PRESERVING
        float factor = gaussian(stdDev, i) * gaussian(depthStdDev, abs(depth - imageLoad(positionDepth, coords + offset).w));
        totalFactor += factor;
#else
        float factor = gaussian(stdDev, i);
#endif
        final += factor * imageLoad(inImage, coords + offset);
    }
#ifdef DISABLE
    imageStore(outImage, coords, imageLoad(inImage, coords));
#else
#ifdef EDGE_PRESERVING
    if(totalFactor > 1e-2)
        final /= totalFactor;
    else final = vec4(0);
#endif
    #ifdef DIRECTION_X
            imageStore(outImage, coords, vec4(final.rgb, 1.0));
    #else
            imageStore(outImage, coords, vec4(final.rgb, 1.0));
    #endif
#endif
}