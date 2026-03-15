struct GBufferPSInput
{
    //float4 PositionVS : POSITION;
    float3 NormalWS   : NORMAL;
    float2 TexCoord   : TEXCOORD;
};

struct GBufferOutput
{
    float4 RT0 : SV_Target0; // Albedo (RGB) + AO placeholder (A=1)
    float4 RT1 : SV_Target1; // Oct-encoded world-space normal (RG), BA unused
    float4 RT2 : SV_Target2; // Roughness (R) + Metalness (G) + EmissiveMask (B)
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
    float Roughness;    // 0 = smooth, 1 = rough
    float Metalness;    // 0 = dielectric, 1 = metal
    float EmissiveMask; // 0 = non-emissive, 1 = emissive
    float _Padding;
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 5 = 80 bytes
};


ConstantBuffer<Material>    MaterialCB              : register(b0, space1);
Texture2D                   DiffuseTexture          : register(t0);
SamplerState                LinearRepeatSampler     : register(s0);

// Expects a world-space 3d normalized unit sphere normal in [-1,1] as an input.
// Packs a normalized unit sphere normal (3d in [-1,1]) into two values (2d in [0,1]) that fit in [R10G10]
float2 OctahedralEncode(float3 n)
{
    // L1 normalization: converts the vector from sphere coordinates to octahedron coordinates. Project sphere (3D) to an octahedron(3D).
    //     - The octahedron is formed by projecting the sphere onto the plane defined by abs(x) + abs(y) + abs(z) = 1  <- equation describes the surface of an octahedron;
    //     - Instead of normalizing with sqrt(x^2 + y^2 + z^2) (L2 norm), we normalize with abs(x) + abs(y) + abs(z) (L1 norm);
    //     - But the range is still x,y,z in [-1,1]. We just projected 3d sphere to 3d octahedron.
    //     - After division we get a vector whose components satisfy: abs(n.x) + abs(n.y) + abs(n.z) = 1. 
    //       But abs(n.x) + abs(n.y) + abs(n.z) can never be zero because the input is a normalized unit sphere normal, so we won't have divide-by-zero issue here.
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    
    // Now encode 3d octahedron coordinates to 2d coordinates.
    //     - The top hemisphere (n.z >= 0) corresponds to the top 4 faces -> you can just take n.xy as a 2D position.
    //     - The bottom hemisphere (n.z < 0) corresponds to the bottom 4 faces -> you can "fold" them up by REFLECTION: (1.0 - abs(n.yx)) * sign(n.xy).
    //     - n.x >= and n.y >= 0 is to cover up the case when n.x or n.y is zero, which would cause the sign() function to return 0 and thus lose directional information. 
    //       By treating zeros as positive, we can preserve the correct octant information in the encoding.
    float2 oct;
    oct.x = n.z >= 0.0 ? n.x : (1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0);
    oct.y = n.z >= 0.0 ? n.y : (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0);
    return oct * 0.5 + 0.5;
}


GBufferOutput main(GBufferPSInput IN)
{
    GBufferOutput output;
    
    float4 texColor = DiffuseTexture.Sample(LinearRepeatSampler, IN.TexCoord);
    
    // Alpha test
    // --
    // TODO: In future to avoid holes in depth buffer, consider having two separate PSOs for deferred G-Buffer rendering: 
    // 1. GBuffer_PS_Opaque (no clip) 
    // 2. GBuffer_PS_AlphaTest (with clip)
    // We would also need to sort m_LoadedMeshParts by whether they need alpha testing, and draw them in two separate loops in the Render() function.
    clip(texColor.a - 0.1f);
    
    float3 albedo       = (MaterialCB.Diffuse * texColor).rgb;
    float3 normalWS     = normalize(IN.NormalWS);
    float2 octNormal = OctahedralEncode(normalWS); // pack 3d to 2d for storage in [R10G10]
    
    output.RT0 = float4(albedo, 1.0); // AO=1 placeholder
    output.RT1 = float4(octNormal, 0.0, 0.0);
    output.RT2 = float4(MaterialCB.Roughness, MaterialCB.Metalness, MaterialCB.EmissiveMask, 0.0);
    
    return output;
}