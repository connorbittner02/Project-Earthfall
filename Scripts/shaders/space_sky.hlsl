// M_SpaceSky. The sky is addressed by view direction, so it sits at infinity with
// no parallax. The backdrop is ESO's visible-light Milky Way panorama (S. Brunier,
// CC BY 4.0) in galactic coordinates, with its black floor crushed to true dark
// space and procedural point stars overlaid so they stay pin-sharp at 4K.
// Tuning = (panorama gain, star gain, black floor, unused).
float3 Main(float3 CamVec, Texture2D SkyTex, SamplerState SkyTexSampler, float4 Tuning)
{
    float3 d = normalize(-CamVec);
    // Tip the galactic plane ~60 degrees off the ecliptic, as in the real sky.
    d = RotateAxis(d, normalize(float3(1.0, 0.35, 0.0)), 1.05);
    float2 uv = DirToUV(d);
    float3 c = Texture2DSampleLevel(SkyTex, SkyTexSampler, uv, 0).rgb;

    float3 milkyWay = max(c - Tuning.z, 0.0);
    milkyWay = pow(milkyWay, 1.2) * Tuning.x;
    float starlight = dot(c, float3(0.30, 0.45, 0.25));

    // Three brightness tiers on a 3D lattice: a few bright stars (2-4), a middle
    // tier (~1) and a dense faint field (0.3-0.6). Low-frequency noise clusters
    // them, and density thickens into the galactic band.
    float band = saturate(starlight * 2.5);
    float cluster = VNoise(d * 5.0 + 11.0) * VNoise(d * 13.0 + 3.0) * 2.4;
    float3 stars = float3(0.0, 0.0, 0.0);
    for (int tier = 0; tier < 3; tier++)
    {
        float scale = tier == 0 ? 95.0 : (tier == 1 ? 260.0 : 620.0);
        float chance = tier == 0 ? 0.06 : (tier == 1 ? 0.13 : 0.24);
        float lowLevel = tier == 0 ? 2.0 : (tier == 1 ? 0.8 : 0.30);
        float highLevel = tier == 0 ? 4.0 : (tier == 1 ? 1.5 : 0.60);
        float3 g = d * scale;
        float3 cell = floor(g);
        float3 f = g - cell;
        float3 h = Hash33(cell + tier * 19.19);
        float dist = length(f - (0.25 + 0.5 * h));
        float presence = step(1.0 - chance * (0.35 + cluster + 0.45 * band), Hash13(cell * 1.7 + 5.3 + tier));
        float level = lerp(lowLevel, highLevel, Hash13(cell + 91.7));
        float radius = tier == 0 ? 0.11 : (tier == 1 ? 0.17 : 0.24);
        float core = saturate(1.0 - dist / radius);
        core = core * core * core;
        // Colour temperature from cool blue-white through white to warm orange.
        float temperature = Hash13(cell + 13.1);
        float3 tint = temperature < 0.5
            ? lerp(float3(0.62, 0.74, 1.00), float3(1.00, 0.98, 0.95), temperature * 2.0)
            : lerp(float3(1.00, 0.98, 0.95), float3(1.00, 0.70, 0.45), temperature * 2.0 - 1.0);
        stars += tint * core * level * presence;
    }

    return milkyWay + stars * Tuning.y;
}
