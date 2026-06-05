#include "IBL_Helpers.hlsli"

// b0 — 1 constant: uint OutputSize (= 32)
// t0 — TextureCube<float4> SourceCubemap
// u0 — RWTexture2DArray<float4> OutputIrradiance (array of 6 faces, mip 0)
// s0 — LinearClamp static sampler

cbuffer IblIrradianceCB : register(b0)
{
    uint OutputSize; // = 32
}

TextureCube<float4> SourceCubemap : register(t0);
RWTexture2DArray<float4> OutputIrradiance : register(u0);

SamplerState LinearClampSampler : register(s0);

// Dispatch(4, 4, 6);
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) // global thread ID: (0,0,0) ... (31,31,5)
{
    uint2 texCoord = DTid.xy;
    uint faceIndex = DTid.z; // z = face 0..5

    // Convert face + texel -> world-space direction N.
    // And N is the direction on "probe" (unit hemisphere) that this texel represents, for which we integrate incoming radiance (below in the loop).
    // We can think of it as the surface-normal direction the irradiance is being computed for, or equivalently the point on the probe's sphere.
    float2 uv = (texCoord + 0.5) / OutputSize;
    float3 N = FaceUvToDirection(uv, faceIndex);

    // Build TBN basis
    // --
    // Building an orthonormal basis. 
    // N is already normalized. We pick an arbitrary "up" vector that's not parallel to N. We can use (0,1,0) as the "up" vector in most cases.
    // The only cases where this would be a problem is if N was exactly (0,1,0) or (0,-1,0), and that would happen when abs(N.y) == 1.0.
    // On +Y or -Y faces, the normal points straight up or down and abs(N.y) will be = 1, so we can't use (0,1,0) as the "up" vector because it's parallel to N.
    // In that case, we can use (1,0,0) as the "up" vector instead of (0, 1, 0).
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);

    float3 irradiance = 0;
    float samples = 0;

    // Riemann sum over the hemisphere  (~360×90 samples = 32 400, very fast as a one-time pass)
    float dPhi = TWO_PI / 360.0;
    float dTheta = HALF_PI / 90.0;
    
    // We loop over Phi and Theta to cover the hemisphere around the normal N. 
    // For each (phi, theta) pair, we compute the corresponding direction vector in world space, 
    // sample the source cubemap in that direction, and accumulate the irradiance contribution weighted by 
    // cos(theta) * sin(theta) (the Jacobian of the spherical coordinates).
    // --
    // THETA: in local coordinates where ts.z is the axis from "south" of the patch to "north":
    // Theta = 0 -> ts = (0,0,1) -> point at the pole (here aligned with N after we rewrite dir = … + ts.z * N).
    // Theta = PI/2 -> cos(theta)=0, sin(theta)=1 -> ts = (cos Phi, sin Phi, 0) -> points on the equator (rim of the hemisphere), 90° from the pole.
    // So Theta is not "latitude from equator"; it’s colatitude / polar angle from the pole N: from 0 at N out to PI/2 at the horizon of that hemisphere.
    // --
    // In other words - Phi (0 to 2PI) is used represent the 2d rim around the sphere ts = (cos Phi, sin Phi, 0).
    // And Theta (0 to PI/2) represents the "Hight" cos(theta) of the unit sphere: 0 in the center of the sphere (cos pi/2), 1 peak point ON the sphere (cos 0).
    
    // As a result:
    // 1. ts - is a unit hemisphere facing in the +Z direction: ts in ([-1,1], [-1,1], [0,1]). So it's a local space hemisphere.
    // 2. We transform that hemisphere to be oriented around the normal N by doing dir = ts.x * right + ts.y * up + ts.z * N.
    //    So ts just forms scalars for the newly calculated basis TBN. And the hemisphere formed by ts is reoriented to be around N by using the TBN basis vectors as a linear combination.
    // 3. Perform a discrete integral of incoming radiance over the hemisphere oriented around N. The final irradiance will represent a point/dir on probe this texel represents.
    // 4. Then the Diffuse irradiance E(N) for a surface with normal N:
    //    E(N) = INT L_i(w) * dot(N, w) dw
    //          where: - L_i is the incoming radiance from direction w (sampled from the cubemap)
    //                 - dot(N, w) = [Lambert's cosine law, which gives us the cos(theta) term (theta is the angle between N and w)] = cos(theta)
    //                 - dw = [the Jacobian of the spherical coordinates, which gives us the sin(theta) term] = sin(theta) dTheta dPhi   <<-- see more detailes below
    //    Intuition:
    //              cos(theta) (= dot(N*w)) - physics: light hitting a Lambertian surface at a grazing angle delivers less energy per unit area. 
    //                      A direction perpendicular to the surface (theta = 0) has full weight 1; a direction along the horizon (theta = pi/2) 
    //                      has weight 0.
    //              sin(theta) - geometry of integration: when we parameterize the hemisphere with (theta, phi), equal steps in theta and phi 
    //                      do not cover equal solid angle. Near the pole (small theta) a band [theta, theta+dtheta] is tiny; near the equator 
    //                      (theta ~= pi/2) it's large. The factor sin(theta) is the area scaling per unit (dtheta, dphi) so the Riemann 
    //                      sum approximates the true spherical integral.
    //
    //   What if we remove the:
    //              Drop cos(theta): we'd be computing total radiance over the hemisphere, not irradiance. 
    //                      Grazing directions would contribute as much as head-on ones - wrong physically.
    //              Drop sin(theta): bright bands near the pole get over-weighted (too many small-Theta samples per actual solid angle) ->
    //                      result biased toward what's "straight up" relative to N.

    for (float phi = 0; phi < TWO_PI; phi += dPhi)
    {
        for (float theta = 0; theta < HALF_PI; theta += dTheta)
        {
            // Unit hemisphere sample in local space where +Z is the normal direction
            float3 ts = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            
            // Transform the sample to world space oriented around N using the TBN basis
            float3 dir = ts.x * right + ts.y * up + ts.z * N;
            
            // Integrand: incoming radiance from direction 'dir' multiplied by the cosine-weighting term (Lambert's cosine law) and the Jacobian of the spherical coordinates.
            
            // E(N) = INT L_i(w) * dot(N, w) dw = INT_(0,2pi) INT_(0,pi/2) { L_i(w) * cos(theta) * sin(theta) dTheta dPhi } ~= [Riemann integral]
            //     ~= SUM_over_phi_theta { [ L_i(dir) * cos(theta) * sin(theta) ] * dTheta * dPhi }
            
            // NOTE_1:
            //   But, below in shader, we dropped multiplication by [dTheta * dPhi]
            //   Technically we can multiply irradiance by dTheta * dPhi after the loop, but instead of that we do: PI * irradiance / samples.
            //   So PI/samples is not literally the same as dTheta*dPhi for my grid:
            //     * dTheta  = (PI/2)/90, dPhi = 2PI/360  =>  dTheta * dPhi = PI^2 / 32400 
            //     * samples = 360 × 90 = 32400           =>  PI/samples    = PI / 32400
            //   As a result:
            //     dTheta * dPhi = PI * (PI/samples)

            //   So the shader uses: (PI/sampes) * SUM() - which is PI times smaller than the TRUE Riemann integral.
            //   So PI * sum / samples is not "we dropped dTheta*dPhi from integral and recovered it exactly with PI". It's a different scaling: PI * (sample average).
            
            // NOTE_2:
            //   Why do we use PI * sum / samples?
            //      if L = 1  =>  K = INT INT cos(theta) * sin(theta) dTheta dPhi = PI
            //                        let's call: S_0 = SUM_i cos(theta_i) * sin(theta_i) 
            
            //   For K, proper Riemann: K ~= (dTheta * dPhi) * S_0 = (PI^2 / samples) * S_0
            //   In our shader:   K_shader = (PI / samples) * S_0
            // So, our K_shader is not an exact Riemann recovery. 
            
            // NOTE_3:
            //   Let's see what does (PI / samples) mean.
            //   (PI / samples) * S_0 = PI * (S_0 / samples)      <-   where (S_0 / samples) is average of sin * cos on the grid
            //   For a fine uniform grid: 
            //              (S_0 / samples) ~= AVERAGE (MEAN) = <cos, sin> over the (theta, phi) rectangular area =
            //              <cos(theta), sin(theta)> = by definition of MEAN = (1 / AREA of domain) * INT_[0,2PI] INT_[0,PI/2]  { cos(theta) * sin(theta) dTheta * dPhi }
            //
            //                  1. AREA, for my hemisphere patch in (theta,phi) space = (2PI) * (PI/2) = PI^2
            //                  2. INT INT cos(theta) * sin(theta) over that patch = PI
            // So: 
            //              <cos(theta), sin(theta)> ~= PI / PI^2 = 1/PI
            // Then:
            //          Getting back to PI * (S_0 / samples) ~= PI * <cos(theta), sin(theta)> =  PI * (1/PI) = 1         <- when L=1
            //          While the true Irradiance with Riemann is: (PI^2 / samples) * S_0 = PI^2 * (S_0/samples) = PI    <- when L=1
            
            // ------------------ When L=1 -------------------------
            // For constant white L=1, this normalization gives 1, not PI. 
            // Many tutorials store something closer to average incoming radiance (or absorb PI in the later albedo/PI BRDF).
            // That's a convention issue when we hook up deferred lighting — not a bug in our understanding of the hemisphere.
            // --
            // So for constant white L=1 we kinda scale the the average INT INT {cos*sin}=<cos,sin>=PI by PI so that after dividing by AREA=PI^2,
            // we would get irradiance=1 for constant white light L.
            
            // ------------------ FINALLY --------------------------
            // We dropped the per-sample dTheta*dPhi; the shader replaces "multiply the whole sum by dTheta*dPhi" with "multiply the AVERAGE by PI", 
            // using the fact that the kernel integral K=INT INT {cos*sin}=PI. That is similar to Riemann (both turn a sum into an integral estimate) but
            // not identical to (Sum * dTheta * dPhi) for our 360x90 grid - they differ by a factor of PI on this grid.

            // So whether to have or not extra PI depends on PBR convention in Deferred Lighting:
            //   1. If indirectDiffuse = irradiance * albedo; I.e (without /PI); The map is often storing something like
            //      average radiance (shader scale PI/samples).
            //   2. If indirectDiffuse = (albedo/PI) * irradiance, the map should store full irradiance E, and we'd want 
            //      (dTheta*dPhi)*Sum ~= (PI^2/samples)*Sum, not (PI/samples)*Sum.
            // So: 
            //   cos*sin = correct integrand pieces; PI/samples = "normalize the sum using PI", not a strict substitute for
            //   dTheta*dPhi unless we align conventions end-to-end in the lighting shader.
            
            irradiance += SourceCubemap.SampleLevel(LinearClampSampler, dir, 0).rgb * cos(theta) * sin(theta); // we dropped dTheta*dPhi
            samples++;
        }
    }
    
    irradiance = PI * irradiance / samples; // mutiplying irradiance by (PI/samples) instead of dTheta*dPhi
    
    // In the end, from Radiance in high resolution cubemap, we got Irradiance by integrating incoming radiance over the hemisphere around N and applying the cosine-weighting and Jacobian terms.
    OutputIrradiance[uint3(texCoord, faceIndex)] = float4(irradiance, 1.0);
}