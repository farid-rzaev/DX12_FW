struct GBufferPSInput
{
    float4 PositionVS : POSITION;
    float3 NormalVS   : NORMAL;
    float2 TexCoord   : TEXCOORD;
};

struct GBufferOutput
{
    float4 Albedo   : SV_Target0; // RGB: albedo;                           A: specular power (normalized)
    float4 Normal   : SV_Target1; // RGB: view-space normal (packed [0,1]), A: unused
    float4 Position : SV_Target2; // RGB: view-space position,              A: unused
};

struct Material
{
    float4 Emissive;
    //----------------------------------- (16 byte boundary)
    float4 Ambient;
    //----------------------------------- (16 byte boundary)
    float4 Diffuse;
    //----------------------------------- (16 byte boundary)
    float4 Specular;
    //----------------------------------- (16 byte boundary)
    float  SpecularPower;
    float3 Padding;
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 5 = 80 bytes
};


ConstantBuffer<Material>    MaterialCB              : register(b0, space1);
Texture2D                   DiffuseTexture          : register(t0);
SamplerState                LinearRepeatSampler     : register(s0);


GBufferOutput main(GBufferPSInput IN)
{
    GBufferOutput output;
    
    // Sample diffuse texture and apply material color
    float4 texColor = DiffuseTexture.Sample(LinearRepeatSampler, IN.TexCoord);
    
    // Alpha test (same as forward renderer) 
    // --
    // TODO: In future to avoid holes in depth buffer, consider having two separate PSOs for deferred G-Buffer rendering: 
    // 1. GBuffer_PS_Opaque (no clip) 
    // 2. GBuffer_PS_AlphaTest (with clip)
    // We would also need to sort your m_LoadedMeshParts by whether they need alpha testing, and draw them in two separate loops in the Render() function.
    clip(texColor.a - 0.1f);
    
    // Store albedo (diffuse color) and specular power
    output.Albedo = float4((MaterialCB.Diffuse * texColor).rgb, MaterialCB.SpecularPower / 256.0); // Normalize specular power to [0,1]
    
    // Store view-space normal (pack from [-1,1] to [0,1])
    float3 normalVS = normalize(IN.NormalVS);
    output.Normal = float4(normalVS * 0.5 + 0.5, 1.0);
    
    // Store view-space position
    output.Position = float4(IN.PositionVS.xyz, 1.0);
    
    return output;
}