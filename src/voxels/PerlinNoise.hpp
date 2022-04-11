#pragma once

#include <stdint.h>
#include <vector>

/** 
 * Simple C++11 version of Perlin Noise reference implementation
 * (http://mrl.nyu.edu/~perlin/noise/)
**/

class PerlinNoise
{
public:
	/** Constructor using default permutation vector **/
	PerlinNoise();
	
	/** Constructor using a permutation vector generated from the seed **/
	PerlinNoise(unsigned int seed);
	
	/** Copy Constructor **/
	PerlinNoise(const PerlinNoise&);
	PerlinNoise(PerlinNoise&&) =default;
	
	PerlinNoise& operator=(const PerlinNoise&);
	PerlinNoise& operator=(PerlinNoise&&) =default;

	~PerlinNoise();
	
	/** @return Noise value in (x, y, z) **/
	double noise(double x, double y = 0.f, double z = 0.f) const;
	/** @return Noise value in (x, y, z) **/
	inline double operator()(double x, double y = 0.f, double z = 0.f) const { return noise(x, y, z); }
	
private:
	uint8_t*	_perm; ///< Permutation Vector

	inline static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
	
	inline static double lerp(double t, double a, double b) { return a + t * (b - a); }
	
	inline static double grad(int hash, double x, double y, double z)
	{
		int h = hash & 15;            
		double u = h < 8 ? x : y,
		v = h < 4 ? y : h == 12 || h == 14 ? x : z;
		return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
	}
	
	static uint8_t defaultPermutation[512]; ///< Default Permutation Vector
};