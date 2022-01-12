#ifndef IRRADIANCE_GLSL
#define IRRADIANCE_GLSL

#include "common.glsl"
#include "ProbeGrid.glsl"

ivec3 probeLinearIndexToGridIndex(uint index, ProbeGrid grid) {
    return ivec3(index % grid.resolution.x, (index / grid.resolution.x) % grid.resolution.y, (index / (grid.resolution.x * grid.resolution.y)) % grid.resolution.z);
}

uint probeLinearIndex(ivec3 index, ProbeGrid grid) {
    return index.x + grid.resolution.x * index.y + grid.resolution.x * grid.resolution.y * index.z;
}

vec3 probeIndexToWorldPosition(ivec3 index, ProbeGrid grid) {
    vec3 gridCellSize = (grid.extentMax - grid.extentMin) / grid.resolution;
	// TODO: Add per-probe offset (< half of the size of a grid cell)
    return grid.extentMin + index * gridCellSize; // + 0.5 * gridCellSize;
}

vec3 probeIndexToWorldPosition(uint index, ProbeGrid grid) {
	return probeIndexToWorldPosition(probeLinearIndexToGridIndex(index, grid), grid);
}

ivec2 probeIndexToColorUVOffset(ivec3 index, ProbeGrid grid) {
    return ivec2(grid.colorRes * ivec2(index.y * grid.resolution.x + index.x, index.z));
}

ivec2 probeIndexToDepthUVOffset(ivec3 index, ProbeGrid grid) {
    return ivec2(grid.depthRes * ivec2(index.y * grid.resolution.x + index.x, index.z)); 
}

/**  Generate a spherical fibonacci point

    http://lgdv.cs.fau.de/publications/publication/Pub.2015.tech.IMMD.IMMD9.spheri/

    To generate a nearly uniform point distribution on the unit sphere of size N, do
    for (float i = 0.0; i < N; i += 1.0) {
        float3 point = sphericalFibonacci(i,N);
    }

    The points go from z = +1 down to z = -1 in a spiral. To generate samples on the +z hemisphere,
    just stop before i > N/2.

*/
vec3 sphericalFibonacci(float i, float n) {
    const float PHI = sqrt(5) * 0.5 + 0.5;
#define madfrac(A, B) ((A)*(B)-floor((A)*(B)))
    float phi = 2.0 * pi * madfrac(i, PHI - 1);
    float cosTheta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0, 1));

    return vec3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta);

#   undef madfrac
}

vec3 sphereToOctahedron(vec3 v) {
    vec3 octant = sign(v);
    // Scale the vector so |x| + |y| + |z| = 1 (surface of octahedron).
    float sum = dot(v, octant); 
    return v / sum;
}

// Assuming v is normalized
vec2 cartesianToPolar(vec3 v) {
    vec2 r = vec2(
        acos(v.z),
        atan(v.y/v.x)
    );
    if(v.x < 0) v.y += pi;
    return r;
}

// Assuming a unit vector
vec3 polarToCartesian(vec2 v) {
    return vec3(cos(v.y) * sin(v.x), sin(v.y) * sin(v.x), cos(v.x));
}

float signNotZero(in float k) {
    return (k >= 0.0) ? 1.0 : -1.0;
}

vec2 signNotZero(in vec2 v) {
    return vec2(signNotZero(v.x), signNotZero(v.y));
}

/** Assumes that v is a unit vector. The result is an octahedral vector on the [-1, +1] square. */
vec2 octEncode(in vec3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 result = v.xy * (1.0 / l1norm);
    if (v.z < 0.0) 
        result = (1.0 - abs(result.yx)) * signNotZero(result.xy);
    return result;
}

/** Returns a unit vector. Argument o is an octahedral vector packed via octEncode,
    on the [-1, +1] square*/
vec3 octDecode(vec2 o) {
    vec3 v = vec3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    return normalize(v);
}

// [0, res - 2[ to [-1, 1] for octahedral lookup
vec2 normalizeLocalTexelCoord(ivec2 coord, uint res) {
    return ((vec2(coord) + 0.5f) * 2.0f / float(res - 2)) - 1.0f;
}

vec2 spherePointToOctohedralUV(vec3 direction) {
    // Should be close to
    //return 0.5 * octEncode(direction) + vec2(0.5);
    
    vec3 octant = sign(direction);

    // Scale the vector so |x| + |y| + |z| = 1 (surface of octahedron).
    float sum = dot(direction, octant);        
    vec3 octahedron = direction / sum;    

    // "Untuck" the corners using the same reflection across the diagonal as before.
    // (A reflection is its own inverse transformation).
    if(octahedron.z < 0) {
        vec3 absolute = abs(octahedron);
        octahedron.xy = octant.xy
                      * vec2(1.0f - absolute.y, 1.0f - absolute.x);
    }

    return octahedron.xy * 0.5f + 0.5f;
}

#extension GL_EXT_debug_printf : enable

#define LINEAR_BLENDING

vec3 sampleProbes(vec3 position, vec3 normal, vec3 toCamera, ProbeGrid grid, sampler2D colorTex, sampler2D depthTex) {
    // Convert position in grid coords
    vec3 gridCellSize = (grid.extentMax - grid.extentMin) / grid.resolution;
    vec3 gridCoords = (position - grid.extentMin) / gridCellSize;
    vec3 biasVector = (0.2 * normal + 0.8 * toCamera) * 0.75 * min(min(gridCellSize.x, gridCellSize.y), gridCellSize.z) * grid.shadowBias;
    vec3 biasedPosition = position + biasVector;
    ivec3 firstProbeIdx = ivec3(gridCoords);
    vec3 alpha = clamp((position - probeIndexToWorldPosition(firstProbeIdx, grid)) / gridCellSize, vec3(0), vec3(1));
    
    vec3 finalColor = vec3(0.0);
    float totalWeight = 0.0;

    // Fallback when all visibility fails
    vec3 fallbackColor = vec3(0.0);
    float totalFallbackWeight = 0.0;

    vec2 uvScaling = vec2(grid.resolution.x * grid.resolution.y, grid.resolution.z);
    
    for(int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & ivec3(1); //ivec3(i % 2,  (i / 2) % 2, (i / 4) % 2);
        ivec3 probeCoords = firstProbeIdx + offset;
        if(any(greaterThan(probeCoords, grid.resolution - 1)) || Probes[probeLinearIndex(probeCoords, grid)] == 0) continue; // Skip off-grid or disabled probes
        vec3 probePosition = probeIndexToWorldPosition(probeCoords, grid);
        vec3 directionToProbe = normalize(probePosition - position);
        vec3 biasedDirectionToProbe = probePosition - biasedPosition;
        vec2 localColorUV = (float(grid.colorRes - 2) / grid.colorRes) * spherePointToOctohedralUV(normal);
        vec2 localDepthUV = (float(grid.depthRes - 2) / grid.depthRes) * spherePointToOctohedralUV(-normalize(biasedDirectionToProbe));

        vec2 colorUV = (vec2(probeIndexToColorUVOffset(probeCoords, grid) + ivec2(1, 1)) / grid.colorRes + localColorUV) / uvScaling;
        vec2 depthUV = (vec2(probeIndexToDepthUVOffset(probeCoords, grid) + ivec2(1, 1)) / grid.depthRes + localDepthUV) / uvScaling;
        // Contribution of this probe, based on its distance from our sample point
        vec3 trilinear = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = 1.0f;
        
        // Smooth backface test
        float backfaceweight = max(0.0001, (dot(directionToProbe, normal) + 1.0) * 0.5);
        weight *= backfaceweight * backfaceweight + 0.2; // This looks very wrong on flat surfaces aligned with the grid, with bright spots under the probes, and, more importantly, huge black spots when a probe is right behind a wall (I guess the visibily test is not enough to cull it in this case)
        //weight *= max(0.0001, (dot(directionToProbe, normal))); // The simpler version works way better for walls
        // But the real solution is probably to nudge the probes out of walls, or disable the problematic ones...

        float fallbackWeight = weight;

        // Moment visibility test
        vec2 depth = textureLod(depthTex, depthUV, 0).xy;
        float mean = depth.x;
        float variance = abs(depth.x * depth.x - depth.y);
        float biasedDistToProbe = length(probePosition - biasedPosition);
        float chebyshevWeight = variance / (variance + max(biasedDistToProbe - mean, 0.0) * max(biasedDistToProbe - mean, 0.0));
        chebyshevWeight = max(pow(chebyshevWeight, 3.0), 0.0);
        weight *= (biasedDistToProbe <= mean) ? 1.0 : chebyshevWeight;
        // I really feel like there's something wrong with my implementation here.

        weight = max(0.000001, weight);

        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= weight * weight * (1.0 / (crushThreshold * crushThreshold)); 
        }

        weight *= trilinear.x * trilinear.y * trilinear.z + 0.001f;
        fallbackWeight *= trilinear.x * trilinear.y * trilinear.z + 0.001f;

        vec3 color = textureLod(colorTex, colorUV, 0).xyz;
        // Non-physical blending, smooths the transitions between probes
        #ifndef LINEAR_BLENDING     
        color = sqrt(color);
        #endif
        
        finalColor += weight * color; 
        totalWeight += weight; 

        fallbackColor += fallbackWeight * color;
        totalFallbackWeight += fallbackWeight;
    }
    if(totalWeight == 0) return vec3(0);

    #ifdef LINEAR_BLENDING
    finalColor *= 1.0 / totalWeight;
    fallbackColor *= 1.0 / totalFallbackWeight;
    return mix(fallbackColor, finalColor, clamp(totalWeight, 0, 1));
    #else
    // Undo the sqrt
    finalColor *= finalColor;
    fallbackColor *= fallbackColor;
    return mix((1.0f / totalFallbackWeight) * fallbackColor, (1.0f / totalWeight) * finalColor, clamp(totalWeight, 0, 1));
    #endif
   }

#endif