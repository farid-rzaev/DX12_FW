#ifndef IBL_HELPERS_HLSLI
#define IBL_HELPERS_HLSLI

static const float PI = 3.14159265359;
static const float TWO_PI = 2.0 * PI;
static const float HALF_PI = PI / 2.0;

// Convert cubemap face index + [0,1] UV to a world-space direction.
// Face ordering matches D3D12's TextureCube convention (+X,-X,+Y,-Y,+Z,-Z).
float3 FaceUvToDirection(float2 uv, uint faceIndex)
{
    // Remap [0,1] to [-1,1]
    float2 st = uv * 2.0 - 1.0;
    
    // Dir vector points from the center of the coordinate system towards a point on a cube plane: +X/-X/+Y/-Y/+Z/-Z.
    // I.e. a vector from the origin outward through the point on the cube (then normalized onto the sphere).
    // It means "the direction this texel represents" = outward from the probe/cube cente
    // -
    // So for the +X face, at the face center you get dir ~ (+1, 0, 0): it goes from the center toward the +X side 
    // of the cube and on into "that direction in the world / sky."
    float3 dir;
    switch (faceIndex)
    {
        case 0:
            dir = float3(1.0, -st.y, -st.x);
            break; // +X
        case 1:
            dir = float3(-1.0, -st.y, st.x);
            break; // -X
        case 2:
            dir = float3(st.x, 1.0, st.y);
            break; // +Y
        case 3:
            dir = float3(st.x, -1.0, -st.y);
            break; // -Y
        case 4:
            dir = float3(st.x, -st.y, 1.0);
            break; // +Z
        default:
            dir = float3(-st.x, -st.y, -1.0);
            break; // -Z
    }
    return normalize(dir);
}

// Van der Corput + Hammersley low-discrepancy sequence
// --
// The Van der Corput radical inverse sequence is a way to generate points that are evenly spread out in the interval [0,1). 
// It's one of the simplest examples of a low-discrepancy sequence, often used in: quasi-Monte Carlo integration
// -- 
// Core idea:
// 1. Take an integer n, write it in some base b, reverse its digits, and place them after the decimal point.
// 2. For base 2 (binary), this is called the bit-reversal permutation.
// Example: n=5 in binary base is 101. Cuz 101 (base 2) = 1 * 2^2 + 0 * 2^1 + 1 * 2^0 (base 10) = 4 + 0 + 1 = 5 (base 10). 
//          Reversing the digits gives us 101 -> 101 (same in this case). Placing it after the decimal point gives us 0.101 in binary.
//          Convert back to decimal: 0.101 (base 2) = 1 * 2^-1 + 0 * 2^-2 + 1 * 2^-3 (base 10) = 0.625 (base 10).
//          So, Van der Corput value for 5 is 0.625.
// --
// For the first few integers, the Van der Corput sequence in base 2 looks like this:
//    | (n) | binary | reversed bits | value |
//    | --- | ------ | ------------- | ----- |
//    | 0   | 0      | 0             | 0.0   |
//    | 1   | 1      | .1            | 0.5   |
//    | 2   | 10     | .01           | 0.25  |
//    | 3   | 11     | .11           | 0.75  |
//    | 4   | 100    | .001          | 0.125 |
//    | 5   | 101    | .101          | 0.625 |
//    | 6   | 110    | .011          | 0.375 |
//    | 7   | 111    | .111          | 0.875 |
// 
// So the sequence begins: 0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875. 
// Notice how it fills gaps very evenly instead of clustering randomly.
// --
// Why it matters:
//   Random samples tend to clump together.
//   Van der Corput points are designed so that:
//    - every new point fills a large empty region
//    - coverage becomes progressively uniform
//   That gives faster convergence in many numerical integration problems.
//
//   Each new sample lands in an underrepresented region.
//   This creates the characteristic "well-spaced" appearance.

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Hammersley(i, N) gives a deterministic low-discrepancy 2D point set. 2D quasi-random sample in [0,1)^2.
// - first coordinate:  i / N (is even spacing)
// - second coordinate: Van der Corput radical inverse (bit-reversed pattern)
// ----
// Why use it?
//   Naive random samples clump/gap -> noisy integration;
//   Hammersley covers the unit square (for integral limits) more uniformly -> faster convergence with fewer samples;
//   Perfect for per-pixel Monte Carlo in precompute passes.
// So think: "smart sample placement" instead of random darts.
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX importance-sampled half-vector in world space
// --
// Converts a 2D sample Xi = (u, v) in [0,1]^2 into a 3D half-vector H distributed according to the GGX normal distribution around surface normal N.
// So more samples where GGX says contribution is likely high, fewer where it is low.
// -- 
// Why this function exists:
//   In specular IBL, brute-force uniform hemisphere sampling is noisy/inefficient, especially for glossy surfaces;
//   So instead of "pick random directions equally", we importance sample the BRDF lobe (GGX).
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    // Remaps artist roughness to microfacet slope parameter: - Smaller a -> tighter/sharper lobe. Larger a -> wider lobe.
    // --
    // Intuition for roughness behavior
    //   * roughness = 0.05: sampled H vectors cluster very close to N -> sharp reflection.
    //   * roughness = 1.0: sampled H vectors spread widely -> blurry reflection.
    // This is exactly why each prefilter mip corresponds to a roughness value.
    float a = roughness * roughness;

    // Uniform azimuth around the normal. Xi.x just chooses angle around the circle.
    float phi = 2.0 * PI * Xi.x; 
    // The most IMPORTANT part of this function is the inverse CDF sampling formula for GGX.
    // It maps Xi.y (that is UNIFORM) to Theta, so that Theta becomes NON-UNIFORM:
    //   * low roughness: many samples near the top (theta ~ 0)
    //   * high roughness: broader spread to larger theta
    // In other words, Xi.y is treated as a uniform on [0,1] and once we use inverse CDF sampling, it becomes a NON-UNIFORM distribution of theta that matches the GGX lobe shape.
    // Cuz we don't use Theta = Xi.y * PI/2 (which would be uniform), we get more samples where GGX is stronger and fewer where it's weaker, which reduces noise and improves convergence.
    // So instead of uniform, we use the formula: cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    // That line is what makes it GGX importance sampling instead of generic hemisphere sampling. That's a nonlinear map from Xi.y to cosTheta.
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Tangent-space half-vector
    // --
    // Same formula that we used in Deferred Irradiance => Unit hemisphere sample in local space where +Z is the normal direction
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // Build TBN to transform H to world space.
    // --
    // Same TBN as in Deferred Irradiance. We rottate tangent-space H into world-space around actual normal N
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    
    // Rotate tangent-space H into world-space around your actual normal N
    // Result is: GGX-distributed half-vector around the real surface normal
    return normalize(T * H.x + B * H.y + N * H.z);
}

#endif // IBL_HELPERS_HLSLI