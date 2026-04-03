// --------------------------------------------------------------------------------
//                                      STRUCTS
// --------------------------------------------------------------------------------

struct PointLight
{
    float4 PositionWS;
    float4 Color;
    float Intensity;
    float Attenuation;
    float2 Padding;
};

struct SpotLight
{
    float4 PositionWS;
    float4 DirectionWS;
    float4 Color;
    float Intensity;
    float SpotAngle;
    float Attenuation;
    float Padding;
};

struct DeferredLightingCommon
{
    matrix InverseViewProjectionMatrix;
    //----------------------------------- (16*4 byte boundary)
    float3 CameraPositionWS;
    uint LightingViewMode;
    // ---------------------------------- (16 byte boundary)
    uint NumPointLights;
    uint NumSpotLights;
    uint2 _Padding;
    //----------------------------------- (16 byte boundary)
};

// --------------------------------------------------------------------------------
//                                    FUNCs
// --------------------------------------------------------------------------------

float DoDiffuse(float3 N, float3 L)
{
    return max(0, dot(N, L));
}

float DoSpecular(float3 V, float3 N, float3 L, float specularPower)
{
    float3 R = normalize(reflect(-L, N));
    float RdotV = max(0, dot(R, V));
    return pow(RdotV, specularPower);
}

float DoAttenuation(float attenuation, float distance)
{
    return 1.0f / (1.0f + attenuation * distance * distance);
}

float DoSpotCone(float3 spotDir, float3 L, float spotAngle)
{
    float minCos = cos(spotAngle);
    float maxCos = (minCos + 1.0f) / 2.0f;
    float cosAngle = dot(spotDir, -L);
    return smoothstep(minCos, maxCos, cosAngle);
}

// Decodes oct-encoded normal from [0,1] back to [-1,1]
float3 OctahedralDecode(float2 f)
{
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// --------------------------------------------------------------------------------
//                                    RESOURCEs
// --------------------------------------------------------------------------------

ConstantBuffer<DeferredLightingCommon> LightPropertiesCB : register(b0);

StructuredBuffer<PointLight> PointLights                 : register(t0);
StructuredBuffer<SpotLight> SpotLights                   : register(t1);

// G-Buffer textures
Texture2D GBufferAlbedo                                  : register(t2);  // RGB=albedo, A=AO
Texture2D GBufferNormal                                  : register(t3);  // RG=oct-encoded WS normal (world-space normal in [0,1]), BA unused
Texture2D GBufferPBR                                     : register(t4);  // R=roughness, G=metalness, B=emissive mask
Texture2D<float> Depth                                   : register(t5);  // Hardware NDC depth [0,1]
// --
SamplerState PointSampler                                : register(s0);


// --------------------------------------------------------------------------------
//                                    MAIN
// --------------------------------------------------------------------------------

float4 main(float2 texCoord : TEXCOORD /*UV space*/) : SV_Target
{    
    float hwDepth = Depth.Sample(PointSampler, texCoord);

    // Early out for skybox pixels (depth buffer cleared to 1.0)
    if (hwDepth >= 1.0) discard;
   
    float2 ndc = texCoord * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clipPos = float4(ndc, hwDepth, 1.0);
    float4 viewPos = mul(LightPropertiesCB.InverseViewProjectionMatrix, clipPos);
    float3 positionWS = viewPos.xyz / viewPos.w;
    
    float4 albedoAO     = GBufferAlbedo.Sample(PointSampler, texCoord);
    float4 normalPacked = GBufferNormal.Sample(PointSampler, texCoord);
    float4 pbrData      = GBufferPBR.Sample(PointSampler, texCoord);
    
    // Unpack normal from [0,1] to [-1,1]
    float3 normalWS = OctahedralDecode(normalPacked.rg);
    float3 albedo   = albedoAO.rgb;
    
    uint lightingViewMode = LightPropertiesCB.LightingViewMode;
    // --
    if (lightingViewMode == 1) return float4(albedo, 1.0);                          // Albedo
    if (lightingViewMode == 2) return float4(normalWS * 0.5 + 0.5, 1.0);            // Normals
    if (lightingViewMode == 3) return float4(pbrData.r, pbrData.g, pbrData.b, 1.0); // PBR
    if (lightingViewMode == 4)                                                      // Depth
    {
        // Raw NDC depth hwDepth in [0, 1] can be hard to read. A simple linear depth is better for visualization:
        float linearDepth = length(LightPropertiesCB.CameraPositionWS - positionWS); // view-space depth
        float normalized = 1.0 - saturate(linearDepth / 100.0); // map [0,100] to [0,1] -> [1,0] (near->bright)
        return float4(normalized.xxx, 1.0);
    }
    
    // Map roughness to a Blinn-Phong exponent: Roughness -> specular power (bridge)
    float roughness = pbrData.r;
    float specularPower = (1.0 - roughness) * 128.0; // rough -> low power, smooth -> high power
    
    // View direction in world-space
    // Lighting loops now use PositionWS and DirectionWS from the light structs
    // (both are already stored on the PointLight/SpotLight structs as PositionWS/DirectionWS)
    float3 V = normalize(LightPropertiesCB.CameraPositionWS - positionWS.xyz);
    
    // Accumulate lighting
    float3 diffuseAccum = float3(0, 0, 0);
    float3 specularAccum = float3(0, 0, 0);
    
    // Point lights
    for (uint i = 0; i < LightPropertiesCB.NumPointLights; ++i)
    {
        PointLight light = PointLights[i];
        
        float3 L = light.PositionWS.xyz - positionWS.xyz;
        float distance = length(L);
        L = L / distance;
        
        float attenuation = DoAttenuation(light.Attenuation, distance);
        
        diffuseAccum += DoDiffuse(normalWS, L) * attenuation * light.Color.rgb * light.Intensity;
        specularAccum += DoSpecular(V, normalWS, L, specularPower) * attenuation * light.Color.rgb * light.Intensity;
    }
    
    // Spot lights
    for (uint j = 0; j < LightPropertiesCB.NumSpotLights; ++j)
    {
        SpotLight light = SpotLights[j];
        
        float3 L = light.PositionWS.xyz - positionWS.xyz;
        float distance = length(L);
        L = L / distance;
        
        float attenuation = DoAttenuation(light.Attenuation, distance);
        float spotIntensity = DoSpotCone(light.DirectionWS.xyz, L, light.SpotAngle);
        
        diffuseAccum += DoDiffuse(normalWS, L) * attenuation * spotIntensity * light.Color.rgb * light.Intensity;
        specularAccum += DoSpecular(V, normalWS, L, specularPower) * attenuation * spotIntensity * light.Color.rgb * light.Intensity;
    }
    
    float3 ambient = float3(0.1, 0.1, 0.1); // Ambient light intensity (constant)
    
    if (lightingViewMode == 5) return float4(diffuseAccum, 1.0);   // Lighting: diffuse  term only
    if (lightingViewMode == 6) return float4(specularAccum, 1.0);  // Lighting: specular term only
    if (lightingViewMode == 7) return float4(ambient, 1.0);        // Lighting: ambient  term only
    
    // Final color: ambient + diffuse + specular
    float3 finalColor = (ambient + diffuseAccum + specularAccum) * albedo.rgb;
    
    return float4(finalColor, 1.0);
}