struct rayPayload {
	vec3 raydx;
	vec3 raydy;
	uint recursionDepth;

	vec4 color; // Result
	float depth;
};