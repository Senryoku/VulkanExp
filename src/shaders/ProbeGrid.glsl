#ifndef PROBEGRID_GLSL
#define PROBEGRID_GLSL

struct ProbeGrid {
    vec3 extentMin;
    float depthSharpness;
    vec3 extentMax;
    float hysteresis;
    ivec3 resolution;
    uint raysPerProbe;
    uint colorRes;
    uint depthRes;
    float shadowBias;
    uint padding[1];
};

#endif