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

static const float PI = 3.14159265359;

// GGX Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith-GGX Geometry (Schlick approximation)
float G_SmithGGX(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// Schlick approximation of Fresnel
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - HdotV), 5.0);
}

  // Full Cook-Torrance specular lobe
float3 CookTorranceSpecular(float3 N, float3 H, float3 V, float3 L, float3 F0, float roughness, out float3 F_out)
{
    float NdotH = saturate(dot(N, H));
    float NdotV = saturate(dot(N, V)) + 1e-5;
    float NdotL = saturate(dot(N, L)) + 1e-5;
    float HdotV = saturate(dot(H, V));
    
    // Surface Roughness
    float D = D_GGX(NdotH, roughness);
    
    // Geometric Self-Shadowing (tiny shadows cast by the surface itself)
    //   Tells how much of the surface is actually visible to both the light and camera.
    // --
    // Note: G_SmithGGX is the most expensive part of the shader, so we use a Schlick approximation here for better performance.
    // The original Smith GGX geometry function involves a costly double loop, but the Schlick approximation provides a good balance between accuracy and performance.
    float G = G_SmithGGX(NdotV, NdotL, roughness);
    
    // Both D & G are used in the Cook-Torrance formula to produce realistic specular highlights that vary based on:
    //  - Surface roughness
    //  - Viewing angle
    //  - Light direction
    // This creates the difference between a shiny plastic ball and a rough rubber ball under the same lighting.
    
    float3 F = F_Schlick(HdotV, F0);
    
    // Summary of the Cook-Torrance BRDF:
    //   - So F is the Fresnel (specular) term
    //   - D+G are the shape/visibility parts of the specular lobe, 
    // and the diffuse term is derived from (1 - F)

    F_out = F;
    return (D * G * F) / (4.0 * NdotV * NdotL);
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
    
    // View direction in world-space
    // Lighting loops now use PositionWS and DirectionWS from the light structs
    // (both are already stored on the PointLight/SpotLight structs as PositionWS/DirectionWS)
    float3 V = normalize(LightPropertiesCB.CameraPositionWS - positionWS.xyz);
    
    // Accumulate lighting
    float3 diffuseAccum = float3(0, 0, 0);
    float3 specularAccum = float3(0, 0, 0);
    
    float roughness = pbrData.r;
    float metalness = pbrData.g;
    
    // ======================================================================================================================
    //                               F0 - Fresnel reflectance at normal incidence
    // ======================================================================================================================
    // F0 is the Fresnel reflectance at 0° viewing angle (normal incidence - looking straight at the surface). 
    // It represents how much light is reflected when you're looking directly perpendicular to a surface.
    // --
    // For Dielectrics (non Metals), F0 Should Be Scalar F0 ~ 0.04 regardless of color. In reality most dielectrics: F0 in [0.02, 0.08], Water: F0 ~ 0.02. 
    // This means that for non-metals, about 4% of the light is reflected at normal incidence, and the rest is transmitted/refracted into the material.
    // The color of the material does not affect F0 for dielectrics, which is why it's typically set to a constant value.
    //   1. Light hits the surface
    //   2. ~4% (F0) bounces off immediately as specular reflection
    //   3. ~96% enters the material, gets absorbed/scattered by the material's pigments, and some re-emerges
    //   4. That re-emerged light represents the albedo color    
    //
    // The (1.0 - F) term then correctly accounts for the fact that whatever light isn't reflected specularly (F) becomes available for 
    //   diffuse contribution. The albedo then modulates that light.
    // So albedo is the color of light that survives the journey through the material and scatters back out, not surface reflection of light directly.
    
    // ======================================================================================================================
    //                          Why F0 is scalar/achromatic for dielectrics?
    // ======================================================================================================================
    //                As F0 is a specular term, and specular reflection is just an intencity highlight, 
    //                it should always be scalar. But then for Metals, F0 has a color... How come?
    // =======================================================================================================================
    
    // For Metals, F0 Should Be Higher (than for dielectrics) and Colored, often F0 > 0.3 and can be as high as 0.9 for 
    //   very reflective metals like silver or gold. 

    // Specular reflection itself is always just intensity - yes! The confusion comes from what causes that intensity to vary across wavelengths (colors).
    // Dielectrics (non-metals):
    //  * The specular reflection at the surface interface is achromatic (colorless)
    //  * When light bounces off glass, plastic, or ceramic, the reflection follows Fresnel equations that depend on 
    //    refractive index (IOR), not on pigment color
    //  * Most dielectrics have similar IOR across all wavelengths (~1.5 for glass, ~1.33 for water)
    //  * Result: F0 ~ 0.04 for all colors equally -> scalar value
    // Metals:
    //  * Metals have a frequency-dependent (wavelength-dependent) refractive index!!!!
    //  * Different wavelengths (R, G, B) have different absorption and reflection behaviors
    //  * Gold reflects red/yellow wavelengths more strongly than blue -> colored F0 like (1.0, 0.766, 0.336)
    //  * Copper reflects red wavelengths more than green/blue
    //  * This is physical - it's baked into the material's electromagnetic properties, not added pigment
    
    // Metalic mostly derived from the albedo as most of the light is reflected and we take/reflect the color from the surface (albedo).
    // Metallic F0 values are typically 0.3+:
    //   Gold: F0 ~ (1.0, 0.766, 0.336)
    //   Copper: F0 ~ (0.955, 0.637, 0.538)
    //   Iron: F0 ~ (0.562, 0.565, 0.578)
    
    // =======================================================================================================================
    //                                  More scintific and precise explanation
    // =======================================================================================================================
    
    // Physically F0 is a scalar for simple dielectrics and a colored (wavelength-dependent) value for metals. 
    // The shader encodes that difference by treating F0 as a float3 and blending between a constant dielectric F0 
    // and a colored F0 derived from the albedo using the metalness factor.

    // Why (intuition):
    // 1. Dielectrics (plastics, glass, most non-metals)
    //   * Light at the surface mostly either reflects a little (specular) or transmits into the material and 
    //     is scattered/absorbed then re-emitted (diffuse).
    //   * Fresnel at normal incidence is determined by the (real) index of refraction (IOR):
    //       F0 = ((n1 - n2) / (n1 + n2))^2 - that is a scalar and almost achromatic for most dielectrics -> typical ~0.02-0.08 (~4%).
    // Intuition: the visible color of a dielectric comes from the light that goes in and interacts with pigments / internal scattering (diffuse),
    //            so the specular reflection is small and essentially neutral (monochrome).
    
    // 2. Metals (conductors)
    //    Metals do not let light travel into a colored diffuse layer; instead light interacts with free electrons and is reflected with 
    //      wavelength dependent strength.
    //    The normal-incidence reflectance for conductors uses the complex refractive index n + i*k. For a single wavelength the formula becomes:
    //      R = ((n-1)^2 + k^2) / ((n+1)^2 + k^2) - different n,k per wavelength -> colored F0.
    // Intuition: metals are like colored mirrors - the specular reflection itself is tinted, and there is effectively no diffuse contribution.

    // How that maps to the shader
    //   * Shader uses float3 F0 = lerp(float3(0.04), albedo, metalness);
    //     * metalness = 0 -> F0 = neutral dielectric (monochrome).
    //     * metalness = 1 -> F0 = albedo (shader treats albedo as the metal’s specular F0).
    //   * Diffuse is modulated/killed by (1 - metalness) and also by (1 - F) so:
    //     * Dielectric: small neutra/acromatic specular + diffuse colored albedo.
    //     * Metal: diffuse removed, color comes from specular (F) which is tinted by F0. And is often tinted by albedo proportionally to metalness.    
    
    // Practical artist / engine notes
    //  * In the metallic workflow the baseColor/albedo texture must be authored differently:
    //    * For dielectrics - it’s the diffuse albedo (pigment).
    //    * For metals      - it should encode the surface reflectance (F0) — artists usually paint the baseColor so that metal pixels are the metal F0.
    //  * Ensure textures are in linear space for lighting calculations. Cuz artists often author albedo in sRGB space,
    //      so you may need to convert it to linear space before using it in lighting calculations in the shader.  
    //  * If you need higher physical accuracy, store measured n,k (or precomputed RGB F0) separately instead of overloading albedo.
    
    // Why Specular reflection has color, but Diffuse reflection doesn't have color (achromatic)?
    //  * Specular - is light reflected at (or very near) the surface (didn't get too deep to sample pigments) -
    //    it hasn’t sampled the material’s pigments/absorption, so its color comes from the surface optical constants.
	//  * Diffuse - is light that enters deep in the material, scatters many times, is selectively absorbed by pigments, then re-emerges -
    //    that multiple scattering + wavelength-dependent absorption is what gives the diffuse color.
    
    // =======================================================================================================================
    //                              How to encode this difference in the shader?
    // =======================================================================================================================
    
    // Key Hint:
    //   Dielectrics:  specular intensity = 0.04 (same for R, G, B) + diffuse color = albedo (R, G, B vary)
    //   Metals:       specular intensity = varies per wavelength = colored F0 (take from albedo but tinted by metalness) 
    //                                                             + diffuse color (= almost zero [metals barely have diffuse])
    
    // In my shader below: F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    // This is blending between "colorless specular" (for dielectric) and "colored specular" (for metal) — capturing this fundamental physical difference.

    // So we blend between the two based on the metalness value. This allows us to use a single float3 F0 value for both types of materials 
    // in our shader. So when it's less metallic (more diffuse), we use the constant dielectric F0 of 0.04, and as it becomes more metallic, we blend towards the colored F0 derived from the albedo.
    
    // =======================================================================================================================
    
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    
    // Point lights
    for (uint i = 0; i < LightPropertiesCB.NumPointLights; ++i)
    {
        PointLight light = PointLights[i];
        
        float3 L = light.PositionWS.xyz - positionWS.xyz;
        float distance = length(L);
        L = L / distance;
        
        float3 H      = normalize(V + L);
        float  NdotL  = saturate(dot(normalWS, L));
        float  atten  = DoAttenuation(light.Attenuation, distance);

        float3 F;
        float3 spec  = CookTorranceSpecular(normalWS, H, V, L, F0, roughness, F);
        float3 kD    = (1.0 - F) * (1.0 - metalness);   // metals have no diffuse
        float3 diff  = kD * albedo / PI;

        diffuseAccum  += diff  * NdotL * atten * light.Color.rgb * light.Intensity;
        specularAccum += spec  * NdotL * atten * light.Color.rgb * light.Intensity;
    }
    
    // Spot lights
    for (uint j = 0; j < LightPropertiesCB.NumSpotLights; ++j)
    {
        SpotLight light = SpotLights[j];
        
        float3 L = light.PositionWS.xyz - positionWS.xyz;
        float distance = length(L);
        L = L / distance;
        
        float spotIntensity = DoSpotCone(light.DirectionWS.xyz, L, light.SpotAngle);
        float atten         = DoAttenuation(light.Attenuation, distance);
        
        float3 H = normalize(V + L);
        float NdotL = saturate(dot(normalWS, L));

        float3 F;
        float3 spec = CookTorranceSpecular(normalWS, H, V, L, F0, roughness, F);
        
        float3 kD = (1.0 - F) * (1.0 - metalness); // metals have no diffuse
        float3 diff = kD * albedo / PI;

        diffuseAccum += diff * NdotL * atten * spotIntensity * light.Color.rgb * light.Intensity;
        specularAccum += spec * NdotL * atten * spotIntensity * light.Color.rgb * light.Intensity;
    }
    
    float3 ambient = float3(0.1, 0.1, 0.1); // Ambient light intensity (constant)
    
    if (lightingViewMode == 5) return float4(diffuseAccum, 1.0);   // Lighting: diffuse  term only
    if (lightingViewMode == 6) return float4(specularAccum, 1.0);  // Lighting: specular term only
    if (lightingViewMode == 7) return float4(ambient, 1.0);        // Lighting: ambient  term only
    
    // Final color: ambient + diffuse + specular
    //float3 finalColor = (ambient + diffuseAccum + specularAccum) * albedo.rgb;
    float3 finalColor = ambient * albedo + diffuseAccum + specularAccum;
    
    float emissiveMask = pbrData.b;
    finalColor += albedo * emissiveMask * 2.0; // simple emissive boost
    
    return float4(finalColor, 1.0);
}