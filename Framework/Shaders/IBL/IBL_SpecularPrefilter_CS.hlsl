// Builds a cubemap where:
// - each mip level = one roughness level;
// - each pixel direction N on that face stores:
//   "if a surface normal points to N, what average specular environment reflection should it get at this roughness?"
// 
// So unlike irradiance (diffuse), this keeps VIEW-DEPENDENT glossy behavior by prefiltering with a GGX-like distribution.

#include "IBL_Helpers.hlsli"

cbuffer IblSpecularCB : register(b0)
{
    uint OutputSize;
    float Roughness;
    float _Pad0;
    float _Pad1;
}

TextureCube<float4> SourceCubemap   : register(t0);

RWTexture2DArray<float4> OutputMip  : register(u0);

SamplerState LinearClampSampler     : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputSize || DTid.y >= OutputSize || DTid.z >= 6)
    {
        return;
    }

    float2 uv = (DTid.xy + 0.5) / OutputSize;
    float3 N = FaceUvToDirection(uv, DTid.z); // same as in IBL_IrradianceConv_CS.hlsl
    // V = N: common simplification during prefilter bake (we pre-integrate over one variable; BRDF LUT handles the rest later).
    float3 V = N;

    float3 prefilteredColor = 0.0;
    float totalWeight = 0.0;
    
    // 1024 is a common safe value for high quality IBL prefiltering.
    // 256: fast, may be acceptable; 512: good middle; 1024: high quality default
    const uint SAMPLE_COUNT = 1024;
    
    // Then Monte Carlo integration loop:
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Xi: 2D quasi-random sample in [0,1]^2.
        // -- 
        // Why use [0,1]^2 at all?
        //   Monte Carlo needs many random samples. The easiest generic domain is a 2D box:
        //     Xi.x -> used as azimuth phi (angle around the normal)
        //     Xi.y -> used as elevation via cosTheta (how tilted H is from N)
        // In ImportanceSampleGGX:
        //     float phi = 2.0 * 3.14159265359 * Xi.x;
        //     float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
        // That's the standard trick: map uniform Xi=(u,v) -> spherical angles -> 3D vector on a local hemisphere (tangent space), then rotate into world space with TBN.
        // So: square -> hemisphere around N -> world-space H -> reflected L on the full sphere -> cubemap sample.
        // --
        // Contrast with irradiance shader:
        //   In irradiance, we also start from a direction N on the cubemap face, but we loop phi/theta on the hemisphere explicitly (nested loops over angles).
        //   In specular prefilter, we loop 1024 Hammersley points instead, because we w    ant samples BIASED TOWARD the GGX lobe (importance sampling), 
        //   not a uniform grid on the hemisphere.
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        
        // H: half-vector sampled according to GGX distribution.
        // --
        // Hemisphere around N - a 3D half-vector H that represents a microfacet orientation. 
        // Importance-sampled to focus on directions that contribute more to the final reflection, especially at higher roughness levels.
        float3 H = ImportanceSampleGGX(Xi, N, Roughness);
        
        // L: reflected light direction from V around H.
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        // NdotL: visibility/weight term (ignore below horizon).
        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            prefilteredColor += SourceCubemap.SampleLevel(LinearClampSampler, L, 0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, 1e-4);
    OutputMip[uint3(DTid.xy, DTid.z)] = float4(prefilteredColor, 1.0);
}