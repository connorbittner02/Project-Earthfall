// M_FXVolume. Translucent unlit "volume" drawn on simple closed meshes: ash columns,
// mushroom caps, steam and splash crowns. Returns (colour, opacity).
// Density is highest where the view ray passes through the middle of the mesh
// and falls to nothing at the silhouette, and scrolling noise in mesh space eats
// into it, so a cylinder or sphere reads as a billowing mass rather than a solid.
// Lighting is a wrapped sun term on the mesh normal with darker noise hollows.
// Tint = (colour, opacity). Shape = (edge softness exponent, noise contrast,
// noise scale, upward scroll speed).
float4 Main(float3 NormalWS, float3 CamVec, float3 LocalPos, float3 Sun, float Time, float4 Tint, float4 Shape)
{
    float3 N = normalize(NormalWS);
    float mu = abs(dot(N, normalize(CamVec)));
    float core = pow(mu, Shape.x);

    // Mesh space is 100 units across; the billows rise along local Z.
    float3 q = LocalPos * (Shape.z / 50.0) + float3(0.0, 0.0, -Time * Shape.w);
    float billow = Fbm(q, 4);
    float fine = VNoise(q * 3.1 + 7.0);
    float noise = saturate(billow * 1.5 - 0.20 + (fine - 0.5) * 0.35);

    // Pyroclastic edge: a second, finer field carves lobes into the silhouette,
    // and the erosion bites hardest where the view ray only grazes the mesh, so
    // the outline breaks into billows instead of following the cylinder or sphere.
    float lobes = Fbm(q * 2.3 + float3(11.0, 5.0, 0.0), 3);
    float grazing = 1.0 - mu;
    // Contrast (Shape.y) only ever removes density, so a high value opens holes
    // and wisps instead of filling the mesh in.
    float density = core * (0.45 + 1.05 * noise)
                  - (1.0 - noise) * 0.36 * Shape.y
                  - (1.0 - lobes) * grazing * grazing * 0.85 * Shape.y;
    density = smoothstep(0.05, 0.70, saturate(density));
    // A fast-scrolling part is a stem (a cylinder, 100 units tall): fade its two
    // flat ends so they never show through the cap or sit on the ground as a disc.
    density *= lerp(1.0, smoothstep(50.0, 34.0, abs(LocalPos.z)), saturate(Shape.w * 2.0 - 0.6));

    float wrap = saturate(dot(N, normalize(Sun)) * 0.55 + 0.45);
    float light = (0.30 + 0.85 * wrap) * (0.60 + 0.55 * noise);
    return float4(Tint.rgb * light, saturate(density * Tint.a));
}
