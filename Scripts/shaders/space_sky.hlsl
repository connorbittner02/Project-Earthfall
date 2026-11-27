// M_SpaceSky. The sky is addressed by view direction, so it sits at infinity with
// no parallax. The NASA WISE all-sky mosaic is false-colour infrared (blue
// starlight band, green dust emission); it is regraded here to a natural
// Milky Way and overlaid with procedural point stars that stay pin-sharp at 4K.
// Tuning = (panorama gain, star gain, black floor, unused).
float3 Main(float3 CamVec, Texture2D SkyTex, SamplerState SkyTexSampler, float4 Tuning)
{
    float3 d = normalize(-CamVec);
    // Tip the galactic plane ~60 degrees off the ecliptic, as in the real sky.
    d = RotateAxis(d, normalize(float3(1.0, 0.35, 0.0)), 1.05);
    float2 uv = DirToUV(d);
    float3 c = Texture2DSampleLevel(SkyTex, SkyTexSampler, uv, 0).rgb;

    float dust = saturate(c.g - max(c.r, c.b) * 0.85);
    float starlight = saturate(c.b - dust * 0.5);
    float3 milkyWay = starlight * float3(1.00, 0.88, 0.72)
                    + dust * float3(0.55, 0.26, 0.20)
                    + c.r * float3(0.50, 0.30, 0.20) * 0.3;
    milkyWay = max(milkyWay - Tuning.z, 0.0);
    milkyWay = pow(milkyWay, 1.5) * Tuning.x;

    // Three star layers on a 3D lattice: sparse bright, medium, dense faint.
    // Density follows the galactic band so the field thickens into the Milky Way.
    float band = saturate(starlight * 2.5);
    float3 stars = float3(0.0, 0.0, 0.0);
    for (int layer = 0; layer < 3; layer++)
    {
        float scale = layer == 0 ? 150.0 : (layer == 1 ? 360.0 : 780.0);
        float3 g = d * scale;
        float3 cell = floor(g);
        float3 f = g - cell;
        float3 h = Hash33(cell + layer * 19.19);
        float3 position = 0.25 + 0.5 * h;
        float dist = length(f - position);
        float presence = step(1.0 - (0.16 + 0.45 * band), Hash13(cell * 1.7 + 5.3 + layer));
        float magnitude = pow(Hash13(cell + 91.7), 7.0 - layer * 1.5);
        float radius = 0.20 + 0.16 * magnitude;
        float core = saturate(1.0 - dist / radius);
        core = core * core * core;
        float3 tint = lerp(float3(0.70, 0.80, 1.00), float3(1.00, 0.78, 0.55), Hash13(cell + 13.1));
        stars += tint * core * (0.55 + 9.0 * magnitude) * presence;
    }

    return milkyWay + stars * Tuning.y;
}
