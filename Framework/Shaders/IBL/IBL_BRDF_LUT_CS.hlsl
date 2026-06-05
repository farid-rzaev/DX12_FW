// This compute shader precomputes the BRDF integration for the split-sum approximation used in image-based lighting (IBL).

#include "IBL_Helpers.hlsli"


float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness;
    float k = (r * r) * 0.5;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

RWTexture2D<float2> OutBRDFLUT : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dim;
    OutBRDFLUT.GetDimensions(dim.x, dim.y);
    
    if (DTid.x >= dim.x || DTid.y >= dim.y)
    {
        return;
    }

    // For each pixel in the BRDF LUT, we compute the integrated BRDF for a given NdotV and roughness. 
    // The LUT will store two values (A and B) that can be used in the split-sum approximation of the specular IBL equation.
    // What the split-sum approximation is, in short:
    // - A corresponds to the "scale" factor for the specular reflection based on the Fresnel term and geometry;
    // - B corresponds to the "bias" factor that accounts for the Fresnel reflectance at normal incidence.
    // The split-sum approximation allows us to separate the BRDF integration into a precomputed LUT (this shader) and a runtime 
    //    evaluation that uses the LUT, which is much faster than doing the full integral at runtime.
    // The integration is done using Monte Carlo sampling, where we sample the hemisphere around the normal direction according to the 
    //    GGX distribution (importance sampling) and accumulate the contributions to A and B based on the geometry and Fresnel terms.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(dim);
    float NdotV = uv.x;
    float roughness = uv.y;

    // We are integrating over the hemisphere of possible light directions (L) and half-vectors (H) for a given view direction (V) and roughness.
    // The view direction V is fixed for each pixel in the LUT, and we integrate over all possible N and L directions that correspond to that NdotV and roughness.
    // The normal N is fixed as (0,0,1) because we are precomputing the BRDF for a surface facing upwards. The view direction V is then defined such that its dot product with N gives us the NdotV value we are testing.
    // So for each pixel in the LUT, we are essentially asking: "If the surface normal is (0,0,1) and the view direction has a certain angle (NdotV) with respect to the normal, and the surface has a certain roughness, what is the integrated BRDF response over all possible light directions?"
    
    // We assume V is in the +Z direction, so NdotV = V.z. This is a common simplification for precomputing the BRDF LUT, as we are integrating over all possible N and L directions for a given NdotV and roughness.
    // So we set V = (sqrt(1 - NdotV^2), 0, NdotV) to ensure that the angle between V and the normal (which is (0,0,1)) corresponds to the NdotV value we are testing.
    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float3 N = float3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;

    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Same Hammersley + ImportanceSampleGGX as in IBL_SpecularPrefilter_CS.hlsl, but now we use them to compute the BRDF integration for the split-sum approximation, instead of prefiltering a cubemap.
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0)
        {
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-5);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    float2 brdf = float2(A, B) / SAMPLE_COUNT;
    OutBRDFLUT[DTid.xy] = brdf;
}