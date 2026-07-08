//-----------------------------------------------------------------------------------------------------------------------------------
// SHORT_EPLANATION:
//-----------------------------------------------------------------------------------------------------------------------------------
// This compute shader precomputes the BRDF integration for the split-sum approximation used in image-based lighting (IBL).
// --
// Each LUT texel stores two numbers (A, B) that answer:
//    "For a GGX surface with this roughness, viewed at this grazingness (NdotV), what is the average Specular BRDF response over all 
//     incoming light directions?" It's split into a part that scales with F0 and a part that does not.
// --
// At runtime we combine that with the prefiltered cubemap (environment) via split-sum.
//-----------------------------------------------------------------------------------------------------------------------------------

#include "IBL_Helpers.hlsli"

//-----------------------------------------------------------------------------------------------------------------------------------
// DETAILED_EPLANATION:
//-----------------------------------------------------------------------------------------------------------------------------------
// 1. Rendering equation (from my notion):
// L_o(v) = L_e(v) + INT_[hemisphere] {f_r(v,l) * L_i(l) * cos(Theta) dw }    <-  where L_o(v) = OutgoingRadiance; L_e(v) = EmitedRadiance; 
//                                                                                L_i(l) = IncomingRadiance; f_r(v,l) = BRDF
//                                                                                L_i(l) * cos(Theta) dw = Irradiance
// Rendering equation (simplified for SPECULAR IBL):
// L_o_spec(v) ~= INT_[hemisphere] {f_r(v,l) * L_i(l) * cos(Theta) dw }       <-  f_r(v,l) = (Ro_d / PI) + (F(v,h) * D(h) * G(l,v)) / (4 * dot(n,l) * dot(n,v))
//                                                                                where (Ro_d / PI) is a diffuse term, so we can drop it for SPECULAR.
//                                                                                f_r_spec(v,l) = (F(v,h) * D(h) * G(l,v)) / (4 * dot(n,l) * dot(n,v))
// L_o_spec(v) ~= INT f_r(v,l) * L_i(l) * dot(N,L) dw

// -----------------------------------------------------------------------------------------------------------------------------------
// 2. Split-sum approximation (Karis / Epic) separates Environment from BRDF:
// INT f_r * L_i * dot(n,l) dw ~= (INT L_i * D dw) (INT [f_r/D] * dot(n,l) dw)    <-  FIRST term is prefiltered cubemap (environment map) sampled at runtime;
//                                                                                    SECOND term is precomputed in THIS SHADER and stored in a LUT

// So in this shader we compute: LUT = (INT [f_r/D] * dot(n,l) dw)

// Let's put f_r in:
// LUT = INT [f_r/D] * dot(n,l) dw = INT (F(v,h) * D(h) * G(l,v)) / (4 * dot(n,l) * dot(n,v)) * dot(n,l) dw =
//                                 = INT (F(v,h) * D(h) * G(l,v)) / 4*dot(n,v)) dw = [we can take 4*dot(n,v) out of integral]
//                                 = (1/(4 * dot(n,v)) * INT (F(v,h) * D(h) * G(l,v)) dw 

// -----------------------------------------------------------------------------------------------------------------------------------
// 3. Let's factorize the Fresnel (Schlick approximation) term F(v,h) into the LUT:
// F = F_0 + (1 - F_0) * (1 - dot(v,h))^5 = F_0 * (1 - F_c) + F_c                  <- where F_c = (1 - dot(v,h))^5 

// LUT = (1/(4 * dot(n,v)) * INT (F(v,h) * D(h) * G(l,v)) dw = (1/(4 * dot(n,v)) * INT ([F_0 * (1 - F_c) + F_c] * D(h) * G(l,v)) dw_l

//   * and we integrate over ALL incoming light directions l (dw_l) for a given view direction v (which corresponds to a given NdotV) and roughness (which affects D and G).
//   * h = by definition = normalize(l + v)

// Let's change dw to dl for better

// -----------------------------------------------------------------------------------------------------------------------------------
// 4. NEXT MOVE: stop integrating over L; integrate over H instead.

// STEP_1: Why switch from L to H?
//   Cook-Torrance is a microfacet model: specular comes from microfacets whose normals are H.
//     * Integrating over all L = "all incoming directions"
//     * Integrating over all H = "all microfacet orientations that could reflect V into some L"
//   Same physics, different variable. We switch because:
//     1. GGX gives a nice sampling distribution for H: D(H)
//     2. That lets us Monte Carlo the integral efficiently (importance sampling)

// STEP_2: Jacobian — where dot(n,h) first appears
//   When H is the half-vector between FIXED V and VARIABLE L, the solid angles are related as follows (standard microfacet result, Walter/Karis):
//     * dw_l = 4 * dot(v,h) dw_h
//
//   Intuition (for above formula):
//   A small patch of half-vector directions dw_h maps to a patch of reflected light directions dw_l. Reflection geometry stretches/compresses solid angle. 
//   The stretch depends on how aligned V is with H as (VdotH). 
//   For unit vectors the rate at which reflected directions L spread when you tilt the half-vector H by a small solid angle dw_h is:
//            dw_l / dw_h = 4 * dot(v,h)
//   The factor 4 comes from the reflection mapping [L=2*(VdotH)*H - V] doubling the angle (L moves twice as fast as H), 
//   and dot(v,h) accounts for how oblique the geometry is. There is no cosine with N here - it is purely about the reflection 
//   map geometry, not the surface orientation.
//
//   Let's factorise the dw_l:
//      LUT = (1/(4 * dot(n,v)) * INT ([F_0 * (1 - F_c) + F_c] * D(h) * G(l,v)) dw_l = 
//            (1/(4 * dot(n,v)) * INT ([F_0 * (1 - F_c) + F_c] * D(h) * G(l,v)) (4 * dot(v,h)) dw_h = [two fours cancel each other]
//            (1/dot(n,v)) * INT ([F_0 * (1 - F_c) + F_c] * D(h) * G(l,v)) * dot(v,h) dw_h

// -----------------------------------------------------------------------------------------------------------------------------------
// 4. Use Monte Carlo integration.
// -----------------------------------------------------------------------------------------------------------------------------------

// I = INT_Omega f(x) dw                  <- Where f(x) is some complicated function (in our case: D*G*F*Jacobian).
// 
// There is no closed-form solution, so we MUST APPROXIMATE IT NUMERICALLY:
// A) Naive approach: uniform sampling
//    Pick N random points x_i uniformly on the hemisphere, evaluate f(),
//    take the average, scale by the hemisphere area |Omega|:
//              I ~= (|Omega|/N) * SUM_[i=1->N] f(x_i)
//    This works, but most samples are wasted — if f() is near-zero everywhere except a small lobe, you mostly sample zeros.
// B) Better: IMPORTANCE SAMPLING
//    Instead of sampling uniformly, sample more where f() is large. Pick points from some distribution with PDF = p(x) —
//    where p is large wherever f is large.
//    The correct estimator is:
//              I = INT_[Omega] f(x) dw = E[f(x)/p(x)] ~= (1/N) * SUM_[i=1->N] f(x_i)/p(x_i)   <- valid for any p > 0
//
//    We divide each sample by p(x_i) to undo the bias from non-uniform sampling.
//    
//    ------------  OFFTOP: WHY DOES DIVIDING BY P WORK? --------------
//    |By definition of Expectation, for any PDF p:
//    |             E[f(x)/p(x)] = INT_[Omega] (f(x) / p(x)) * p(x) dw = INT_[Omega] f(x) dw = I
//    -----------------------------------------------------------------
//    |E denotes the expected value (expectation) with respect to the probability distribution whose density is p(x).
//    |Expectation operator by definition is:
//    |             E[g(X)] = INT_[Omega] g(x) * p(x) dx               <- g(x) is a function of the random variable x
//    |                                                                   p(x) is the probability density function and dx
//    |Then by drawing each sample x_i & p(x_i), in descrete form, we get:
//    |             I = E[g(X)] ~= (1/N) * SUM_[i=1->N] g(x_i)
//    -----------------------------------------------------------------
// --------------------------------------------------------------------

// -----------------------------------------------------------------------------------------------------------------------------------
// STEP_3: Let's apply Monte Carlo to my integral.
// -----------------------------------------------------------------------------------------------------------------------------------

//  In our previous step we got LUT:
//      LUT = (1/dot(n,v)) * INT (F(v,h) * D(h) * G(l,v)) * dot(v,h) dw_h                <-  where F = [F_0 * (1 - F_c) + F_c]
//         ~= (1/dot(n,v)) * (1/N) * SUM_[i=1->N] [ F(v,h) * D(h) * G(l,v) * dot(v,h) ] / [ p(h) ]
//
//  And we chose Probability Distribution Function(PDF)=GGX => p(h) = D(h)*(dot(n,h).
//   ------------  OFFTOP: WHY DOES DIVIDING BY p(h) WORK? --------------
//   | Why chose p(h) = D(h)*(dot(n,h)?
//   -----------------------------------------------------------------
//   | Our integrand f contains D(h). So we picked p so that it would have D(h) in it as well.
//   | The closer p gets to f (where f is our integrand), the lower is the variance.
//   | If p=c*f exactly (same shape, different scale), variance would be zero - every sample gives the same value.
//   | p(h)=D(h)*dot(n,h) is the natural PDF for GGX because. D is already the NDF (it is normalized over the hemisphere 
//   | with the cosine weight). ImportanceSampleGGX in our helper file samples exactly this distribution.
//   -----------------------------------------------------------------
//
//  So applying Monte Carlo to LUT with this PDF gives:
//     LUT = (1/dot(n,v)) * (1/N) * SUM_[i=1->N] [F(v,h) * D(h) * G(l,v) * dot(v,h)] / [D(h)*dot(n,h)]
//         = (1/N) * SUM_[i=1->N] (1/dot(n,v)) * [F(v,h) * D(h) * G(l,v) * dot(v,h) / D(h)*dot(n,h)]
//         = (1/N) * SUM_[i=1->N] [F(v,h) * G(l,v) * dot(v,h)] / [dot(n,h) * dot(n,v)]               <-  where F = [F_0 * (1 - F_c) + F_c]


// STEP_4: Let's break it down for our shader.
//   In our shader G_vis = G(l,v) * dot(v,h) / dot(n,h) * dot(n,v)
//   Let's also factorise F:
//      LUT = (1/N) * SUM_[i=1->N] F * G_vis = (1/N) * SUM_[i=1->N] [F_0 * (1 - F_c) + F_c] * G_vis
//          = (1/N) * SUM_[i=1->N] [F_0 * (1 - F_c)] * G_vis + [F_c] * G_vis
//
//   And in our shader:
//      A = [(1 - F_c)] * G_vis     <- "how much does Specular Scale with our material's F0?" - BAKED material-agnostic
//      B = [F_c] * G_vis           <- "how much is the Grazing-Angle Fresnel baseline regardless of F0?" — BAKED material-agnostic
//      F0                          <- supplied at RUNTIME per pixel

// STEP_5: Why A doesn't contain F_0?
//  Notice that we have F_0 in the formula, but we didn't include it in A.
//  We didn't cuz: F_0 is actually CALCULATED AT RUNTIME and A is just a prebaked coefficient that it's multiplied by.
//  So we can rewrite the formula:
//      LUT = (1/N) * F_0 * SUM_[i=1->N] {(1 - F_c) * G_vis} + (1/N) * SUM_[i=1->N] {F_c * G_vis}
//
//  F0 was pulled outside the integral because it is a constant per-pixel material property - it does not depend on H or L.
//
//  As a result, the mathematically correct runtime formula is:
//      specularIBL = prefilteredColor * (F_0 * A + B)

// STEP_6: Using non mathematically correct specularIBL?
//  Even though the mathematically correct formula for specularIBL is the one we mentioned above in STEP_5.
//  Our DeferredLighting shader uses another one (phisically incorrect one):
//      F_ibl = F_Schlick(NdotV, F0) = F0 + (1-F0)*(1-NdotV)^5
//      specularIBL  = prefilteredColor * (F_ibl * A + B);
//  The difference is an extra (1-F0)*(1-NdotV)^5 term. This double-counts the grazing-angle Fresnel - 
//  the grazing behaviour is already baked into B via the integration of F_c over all H directions.
//  Adding a second Fresnel boost at the macro NdotV angle is not derived from the math.
//  -- 
//  Why is it done this way anyway?
//    It is a widespread approximation that came from learnopengl.com tutorials and other resources. 
//    Reasons it persists:
//     - It makes IBL specular look brighter and more dramatic at grazing angles, which many people prefer visually
//     - The error is subtle for mid-roughness materials at non-grazing angles
//     - It is a common copy-paste that spread through many codebases
//  If one wants to stay consistent with the derivation, could just use: F0 instead of F_ibl.

// -----------------------------------------------------------------------------------------------------------------------------------
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
        
        // Reflected from V light direction L
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