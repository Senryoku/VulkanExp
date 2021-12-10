
const float pi = 3.1415926538f;

vec3 probeIndexToWorldPosition(uint index, ivec3 gridResolution, vec3 gridCellSize) {
	ivec3 pos = ivec3(index % gridResolution.x, (index % (gridResolution.x * gridResolution.y)) / gridResolution.z, index / (gridResolution.x * gridResolution.y));
	return pos * gridCellSize  + 0.5 * gridCellSize;
	// TODO: Add per-probe offset (< half of the size of a grid cell)
}

vec3 gridPositionToWorldPosition(uvec3 position, vec3 gridCellSize) {
	return position * gridCellSize + 0.5 * gridCellSize;
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
