layout (local_size_x = 32, local_size_y = 32) in;

layout(set = 0, binding = 0, rgba32f) uniform image2D positionDepth;
layout(set = 0, binding = 1, rgba32f) uniform image2D inImage;
layout(set = 0, binding = 2, rgba32f) uniform image2D outImage;

//#define DISABLE

#define EDGE_PRESERVING

float gaussian(float stdDev, float dist) {
    return (1 / (sqrt(2 * 3.14159) * stdDev)) * exp(-(dist * dist) / (2 * stdDev * stdDev));
}

const int maxDev = 32;

void main()
{
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    vec4 final = vec4(0);
    float depth = imageLoad(positionDepth, coords).w;
    float roughness = imageLoad(inImage, coords).w;
    float stdDev = max(1, maxDev * roughness / max(1, (depth / 2.0))); // FIXME: This is arbitrary.
    float depthStdDev = 1.0;                        // FIXME: Also arbitrary.
    float sqrDev = stdDev * stdDev;
    int window = int(clamp(ceil(sqrt(-2 * sqrDev * log(0.01 * stdDev * sqrt(2 * 3.14159)))), 1, maxDev));
    float totalFactor = 0;
    // TODO: Base the kernel on the roughness. A better approximation should use as much available geometric data as possible (sample direction, depth...)
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
#ifdef EDGE_PRESERVING
    if(totalFactor > 1e-2)
        final /= totalFactor;
    else final = vec4(0);
#endif
#ifdef DISABLE
    imageStore(outImage, coords, imageLoad(inImage, coords));
#else
    #ifdef DIRECTION_X
            imageStore(outImage, coords, vec4(final.rgb, roughness)); // This will be used as input in the next pass, also store the roughness.
    #else
            imageStore(outImage, coords, vec4(final.rgb, 1.0));
    #endif
#endif
}