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

struct Mat
{
    matrix InverseViewProjectionMatrix;
};

struct LightProperties
{
    float3 CameraPositionWS;
    uint NumPointLights;
    uint NumSpotLights;
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
    
    // Using max(1.0f, ...) to guard against SpecularPower=ZERO.
    // Cuz pow(x, 0) = 1 for any x > 0, but pow(0, 0) is undefined and can return NaN!!!
    return pow(RdotV, max(1.0f, specularPower));
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

// --------------------------------------------------------------------------------
//                                    RESOURCEs
// --------------------------------------------------------------------------------

ConstantBuffer<Mat> MatCB                           : register(b0);
ConstantBuffer<LightProperties> LightPropertiesCB   : register(b1);

StructuredBuffer<PointLight> PointLights            : register(t0);
StructuredBuffer<SpotLight> SpotLights              : register(t1);

// G-Buffer textures
Texture2D GBufferAlbedo                             : register(t2); // RGB: albedo;                           A: specular power (normalized)
Texture2D GBufferNormal                             : register(t3); // RGB: view-space normal (packed [0,1]), A: unused
Texture2D<float> Depth                              : register(t4); // Hardware NDC depth [0,1]
// --
SamplerState PointSampler                           : register(s0);


// --------------------------------------------------------------------------------
//                                    MAIN
// --------------------------------------------------------------------------------

float4 main(float2 texCoord : TEXCOORD /*UV space*/) : SV_Target
{
    // Sample G-Buffer
    float4 albedo       = GBufferAlbedo.Sample(PointSampler, texCoord);
    float4 normalPacked = GBufferNormal.Sample(PointSampler, texCoord);
    
    // As Sponza model could have perfectly black texels (e.g., shadow areas, pure black materials), just checking for zero albedo is not enough.
    // So checking for normal as well.
    if (all(albedo == 0) && all(normalPacked == 0))
    {
        discard;
    }
    
    float hwDepth = Depth.Sample(PointSampler, texCoord);
    
    float2 ndc = texCoord * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clipPos = float4(ndc, hwDepth, 1.0);
    float4 viewPos = mul(MatCB.InverseViewProjectionMatrix, clipPos);
    float3 positionWS = viewPos.xyz / viewPos.w;
    
    // Unpack normal from [0,1] to [-1,1]
    float3 normalWS = normalize(normalPacked.xyz * 2.0 - 1.0);
    
    // Reconstruct specular power from albedo.a
    float specularPower = albedo.a * 256.0;
    
    // View direction in view space
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
    
    // Final color: ambient + diffuse + specular
    float3 ambient = albedo.rgb * 0.1; // Simple ambient as fraction of albedo
    float3 finalColor = (ambient + diffuseAccum + specularAccum) * albedo.rgb;
    
    return float4(finalColor, 1.0);
}