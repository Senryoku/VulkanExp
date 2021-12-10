struct ProbeGrid {
    vec3 extentMin;
    float depthSharpness;
    vec3 extentMax;
    float hysteresis;
    ivec3 resolution;
    uint raysPerProbe;
    uint colorRes;
    uint depthRes;
    uint padding[2];
};

const float pi = 3.1415926538f;


ivec3 probeLinearIndexToGridIndex(uint index, ProbeGrid grid) {
    return  ivec3(index % grid.resolution.x, (index / grid.resolution.x) % grid.resolution.y, (index / (grid.resolution.x * grid.resolution.y)) % grid.resolution.z);
}

vec3 probeIndexToWorldPosition(ivec3 index, ProbeGrid grid) {
    vec3 gridCellSize = (grid.extentMax - grid.extentMin) / grid.resolution;
	// TODO: Add per-probe offset (< half of the size of a grid cell)
    return grid.extentMin + index * gridCellSize + 0.5 * gridCellSize;
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
#   define madfrac(A, B) ((A)*(B)-floor((A)*(B)))
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

vec2 spherePointToOctohedralUV(vec3 direction) {
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

// Compute normalized oct coord, mapping top left of top left pixel to (-1,-1)
vec2 normalizedOctCoord(ivec2 fragCoord, uint res) {
    vec2 octFragCoord = ivec2((fragCoord.x - 2) % res, (fragCoord.y - 2) % res);
    // Add back the half pixel to get pixel center normalized coordinates
    return (vec2(octFragCoord) + vec2(0.5f))  *(2.0f / float(res - 2)) - vec2(1.0f, 1.0f);
}

vec3 sampleProbes(vec3 position, vec3 normal, ProbeGrid grid, sampler2D colorTex, sampler2D depthTex) {
    // Convert position in grid coords
    vec3 gridCoords = (position - grid.extentMin) / grid.resolution;
    vec3 gridCellSize = (grid.extentMax - grid.extentMin) / grid.resolution;
    ivec3 firstProbeIdx = ivec3(gridCoords);
    vec3 alpha = clamp((position - probeIndexToWorldPosition(firstProbeIdx, grid)) / gridCellSize, vec3(0), vec3(1));
    
    vec3 finalColor = vec3(0.0);
    float totalWeight = 0.0;
    
    for(int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i / 2, (i / 4) % 2, i % 2);
        ivec3 probeCoords = firstProbeIdx + offset;
        vec3 probePosition = probeIndexToWorldPosition(probeCoords, grid);
        float distToProbe = length(position - probePosition);
        vec3 direction = (position - probePosition) / distToProbe;
        vec2 uv = spherePointToOctohedralUV(direction.xyz);
        ivec2 colorUV = ivec2((grid.colorRes - 2) * uv);
        ivec2 depthUV = ivec2((grid.depthRes - 2) * uv);
        // Contribution of this probe, based on its distance from our sample point
        vec3 trilinear = mix(1.0 - alpha, alpha, offset);
        float weight = 1.0f;
        
        // Smooth backface test
        float backfaceweight = max(0.0001, (dot(-direction, normal) + 1.0) * 0.5);
        weight *= backfaceweight * backfaceweight + 0.2;

        // Moment visibility test
        vec2 depth = texture(depthTex, depthUV).rg;
        float mean = depth.x;
        float variance = abs(depth.x * depth.x - depth.y);
        float chebyshevWeight = variance / (variance + max(distToProbe - mean, 0.0) * max(distToProbe - mean, 0.0));
        weight *= (distToProbe <= mean) ? 1.0 : chebyshevWeight;

        weight = max(0.000001, weight);

        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= weight * weight * (1.0 / (crushThreshold * crushThreshold)); 
        }

        weight *= trilinear.x * trilinear.y * trilinear.z;
        
        vec3 color = texture(colorTex, colorUV).xyz;
        // Non-physical blending, smooths the transitions between probes
        color = sqrt(color);

        finalColor += weight * color; 
        totalWeight += weight; 
    }

    finalColor /= totalWeight;

    // Undo the sqrt
    finalColor *= finalColor;

    return finalColor;
}
