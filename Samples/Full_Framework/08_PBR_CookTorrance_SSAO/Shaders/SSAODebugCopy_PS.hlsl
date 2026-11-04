// File: Shaders/SSAODebugCopy_PS.hlsl (optional helper for AO_Raw / AO_Blurred debug display)

struct PSIn
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

Texture2D<float> AOTex : register(t0);
SamplerState PointClampSampler : register(s0);

float4 main(PSIn input) : SV_Target
{
    float ao = AOTex.Sample(PointClampSampler, input.TexCoord);
    return float4(ao.xxx, 1.0);
}