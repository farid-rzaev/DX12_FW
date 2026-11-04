// File: Shaders/SSAOBlur_PS.hlsl
// Edge-aware bilateral blur (single pass; run twice for H/V).

struct PSIn
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

cbuffer BlurParams : register(b0)
{
    float2 TexelSize;   // (1/width, 1/height)
    float2 Direction;   // (1,0) horizontal or (0,1) vertical

    float SpatialSigma; // e.g. 2.0
    float DepthSigma;   // e.g. 0.02
    float NormalSigma;  // e.g. 32.0 (higher => stricter normal edge)

    float _Pad0;
};

Texture2D<float> AOInput    : register(t0);     // raw or ping-pong AO
Texture2D<float> DepthTex   : register(t1);
Texture2D<float4> NormalTex : register(t2);     // oct-encoded normal rg

SamplerState PointClampSampler : register(s0);

float3 OctDecode(float2 f01)
{
    float2 f = f01 * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// Computes a smooth falloff weight based on distance, following a bell curve (normal distribution).
// What it does:
// • Takes a distance x and a spread parameter sigma
// • Returns a value between 0 and 1
// • Peak value (1.0) when x = 0 (at the center)
// • Falls off smoothly as x increases (farther away)
float Gaussian(float x, float sigma)
{
    return exp(-(x * x) / max(2.0 * sigma * sigma, 1e-6));
}

float main(PSIn input) : SV_Target
{
    const int RADIUS = 3;

    float2 uv = input.TexCoord;
    float centerAO = AOInput.Sample(PointClampSampler, uv);
    float centerDepth = DepthTex.Sample(PointClampSampler, uv);
    float3 centerN = OctDecode(NormalTex.Sample(PointClampSampler, uv).rg);

    // Typically, a depth value of 1.0 represents the far plane or sky—infinitely distant geometry (or nothing at all). These pixels don't need blur because:
    // 1. No AO to blur – Sky has no occlusion information worth preserving; it's usually fully lit or a solid color
    // 2. No meaningful neighbors – Blurring sky pixels with other sky pixels adds no visual benefit
    // 3. Performance – Skip the expensive 7-tap blur loop (7 texture samples + weight calculations) for pixels that don't matter
    // Without this check, the shader would:
    // • Blend AO across the horizon line (where sky meets world geometry)
    // • Waste cycles blurring uniform sky pixels
    // • Potentially create artifacts at the silhouette edge
    if (centerDepth >= 1.0)
        return centerAO;

    float sum = 0.0;
    float wsum = 0.0;

    [unroll]
    for (int i = -RADIUS; i <= RADIUS; ++i)
    {
        float2 suv = uv + Direction * TexelSize * (float) i;

        float ao = AOInput.Sample(PointClampSampler, suv);
        float d = DepthTex.Sample(PointClampSampler, suv);
        float3 n = OctDecode(NormalTex.Sample(PointClampSampler, suv).rg);

        // Nearby samples weigh more, far ones less. Smears AO across edges
        float wSpatial = Gaussian((float) i, SpatialSigma);
        // |Depth diff| small -> same surface OR similar distance -> higher weight 
        float wDepth = Gaussian(abs(d - centerDepth), DepthSigma);
        
        // Why it matters: 
        // a plain spatial blur (wSpatial alone) only looks at screen distance, so it would bleed the dark AO from a crevice onto 
        // the foreground object next to it. That's what "smears AO across edges" means in the other comment. 
        // The depth weight is what stops the blur at silhouettes. That's the "edge-aware/bilateral" part of the blur.
        
        // Normals are normalized, so dot() is just a cos() => surface-orientation similarity test:
        //  - Don't blend AO across a crease / hard edge where normals disagree, even if depth is similar (e.g. floor meeting wall)
        // pow() - make the edge stricter
        //  - dot falls off gently. Raising to a high power (e.g. 32) makes it peaky
        // [So only nearly-matching normals keep weight. NormalSigma is "how intolerant of normal mismatch".]
        float nDot = saturate(dot(centerN, n));
        float wNormal = pow(nDot, NormalSigma);

        // Why multiply all?
        // Each factor is a soft "permission":
        //  * wSpatial - nearby in screen space?
        //  * wDepth   - similar depth?
        //  * wNormal  - similar surface orientation?
        // A sample only contributes if it's close AND same depth plane AND same-ish normal.
        // If we added them, a sample could score high on space alone and still smear across an edge.
        // Multiplication kills the weight when any one channel says "edge".
        float w = wSpatial * wDepth * wNormal;
        
        // Summary:
        // Gaussian = smooth similarity falloff.
        //   Spatial = neighborhood.
        //   Depth = don't cross depth edges.
        //   Normal + pow = don't cross geometric creases. 
        // Multiply = all must agree, or the sample is ignored.
        
        sum += ao * w;
        wsum += w;
    }

    if (wsum < 1e-6)
        return centerAO;

    return sum / wsum;
}