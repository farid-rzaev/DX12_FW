// File: Shaders/SSAO_PS.hlsl
// SM 5.1 pixel shader, fullscreen triangle input from FullscreenTriangle_VS.hlsl

struct PSIn
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

static const uint SSAO_KERNEL_SIZE = 32;

cbuffer SSAOParams : register(b0)
{
    float4x4 InvProjection;     // clip -> view
    float4x4 View;              // world -> view (for normal transform)
    float4x4 Projection;        // view -> clip (for sample reprojection)

    float2 NoiseScale;          // screenSize / noiseTexSize (e.g. width/4, height/4)
    float Radius;               // e.g. 0.5
    float Bias;                 // e.g. 0.025

    float Power;                // e.g. 2.0
    float Intensity;            // e.g. 1.0
    float2 _Pad0;

    // Fixed list of sample offsets in tangent space.
    // Conventionally each Kernel[i].xyz lives in a unit hemisphere around +Z:
    //   z >= 0 (above the surface)
    //   length often < 1, and many kernels bias samples toward the origin (more near-surface samples)
    float4 Kernel[SSAO_KERNEL_SIZE]; // xyz - used
};

Texture2D<float> DepthTex   : register(t0);  // hardware depth [0,1]
Texture2D<float4> NormalTex : register(t1);  // oct-encoded normal in rg ([0,1])
Texture2D<float4> NoiseTex  : register(t2);  // tiled random vecs (xy in [-1,1], z=0)

SamplerState PointClampSampler : register(s0);
SamplerState PointWrapSampler  : register(s1);

float3 OctDecode(float2 f01)
{
    float2 f = f01 * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

float3 ReconstructViewPos(float2 uv, float depth)
{
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clip = float4(ndc, depth, 1.0);
    float4 view = mul(InvProjection, clip);
    return view.xyz / max(view.w, 1e-6);
}

float main(PSIn input) : SV_Target
{
    float2 uv = input.TexCoord;
    float centerDepth = DepthTex.Sample(PointClampSampler, uv);

    // Sky/background
    if (centerDepth >= 1.0)
        return 1.0;

    float3 P = ReconstructViewPos(uv, centerDepth);

    float3 N_ws = OctDecode(NormalTex.Sample(PointClampSampler, uv).rg);
    float3 N_vs = normalize(mul((float3x3) View, N_ws));

    // Why use random vector randVec for T?
    // The problem: every pixel with the same normal gets the same TBN, so the same view-space sample directions. 
    //              You get banding / structured patterns along flat walls.
    float3 randVec = normalize(NoiseTex.Sample(PointWrapSampler, uv * NoiseScale).xyz * 2.0 - 1.0);
    float3 T = randVec - N_vs * dot(randVec, N_vs);
    {   // If randVec is almost parallel to N, T can blow up. Common fix:
        if (dot(T, T) < 1e-4)
        {
            T = abs(N_vs.z) < 0.999 ? cross(float3(0,0,1), N_vs) : cross(float3(1,0,0), N_vs);
        }
        T = normalize(T);
    }
    float3 B = cross(N_vs, T);
    float3x3 TBN = float3x3(T, B, N_vs);

    float occlusion = 0.0;

    [unroll]
    for (uint i = 0; i < SSAO_KERNEL_SIZE; ++i)
    {
        float3 sampleDirVS = mul(TBN, Kernel[i].xyz);
        float3 samplePosVS = P + sampleDirVS * Radius;

        float4 sampleClip = mul(Projection, float4(samplePosVS, 1.0));
        float2 sampleUV = sampleClip.xy / max(sampleClip.w, 1e-6);
        sampleUV = sampleUV * float2(0.5, -0.5) + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = DepthTex.Sample(PointClampSampler, sampleUV);
        if (sampleDepth >= 1.0)
            continue;

        float3 sampleDepthVSPos = ReconstructViewPos(sampleUV, sampleDepth);

        // D3D view-space forward is usually +Z or -Z depending convention; use absolute delta robustness.
        float dz = abs(P.z - sampleDepthVSPos.z);
        float rangeWeight = saturate(1.0 - dz / (Radius * 2.0));

        // If geometry at sampled uv is in front of the sample point => occluder
        float occ = (sampleDepthVSPos.z <= samplePosVS.z - Bias) ? 1.0 : 0.0;
        occlusion += occ * rangeWeight;
    }

    occlusion /= (float) SSAO_KERNEL_SIZE;
    float ao = 1.0 - occlusion * Intensity;
    ao = pow(saturate(ao), Power);

    return ao;
}
