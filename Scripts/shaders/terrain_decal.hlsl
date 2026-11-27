// M_TerrainDestruction. A planet-sized deferred decal that paints the destruction
// simulations onto streamed terrain. The pixel's position is turned back into
// longitude and latitude and used to sample the same equirectangular render
// targets the orbital surface shader reads, so a crater seen from orbit is the
// same crater on the ground.
//   RelPos = pixel position relative to the camera, CamPos = camera position in
//   planet-local space. Terrain.x = hand-over fade 0..1.
//   Mode 0 returns (base colour, opacity); Mode 1 returns (emissive, 0).
float4 Main(float3 RelPos, float3 CamPos,
            Texture2D MaskTex, SamplerState MaskTexSampler,
            Texture2D AshTex, SamplerState AshTexSampler,
            Texture2D CraterTex, SamplerState CraterTexSampler,
            Texture2D WaveTex, SamplerState WaveTexSampler,
            Texture2D FireTex, SamplerState FireTexSampler,
            Texture2D NightTex, SamplerState NightTexSampler,
            float3 Sun, float Time, float4 Terrain, float Mode)
{
    float3 n = normalize(RelPos + CamPos);
    // Left-handed world: east is -Y, so longitude is atan2(-Y, X). V runs south.
    float2 uv = float2(atan2(-n.y, n.x) / 6.2831853 + 0.5,
                       0.5 - asin(clamp(n.z, -1.0, 1.0)) / 3.14159265);

    float3 Mask = Texture2DSampleLevel(MaskTex, MaskTexSampler, uv, 0).rgb;
    float3 Ash = Texture2DSampleLevel(AshTex, AshTexSampler, uv, 0).rgb;
    float2 crater = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv, 0).rg;
    float3 Wave = Texture2DSampleLevel(WaveTex, WaveTexSampler, uv, 0).rgb;
    float2 Fire = Texture2DSampleLevel(FireTex, FireTexSampler, uv, 0).rg;

    float scorch = saturate((Mask.r - 0.25) / 0.60);
    float heat = saturate(Mask.g);
    float deposit = saturate(Ash.b);
    float flood = saturate(Wave.b * 3.0);
    float crest = smoothstep(0.10, 0.60, abs((Wave.g - 0.5) * 20.0));
    float night = 1.0 - smoothstep(-0.20, 0.04, dot(n, Sun));

    if (Mode < 0.5)
    {
        // Layers composited front to back in the surface shader's order.
        float3 col = float3(0.0, 0.0, 0.0);
        float a = 0.0;
        float x;
        // A decal's emissive is scaled by its opacity, so anything that must glow
        // needs cover beneath it: the night side (a daytime photograph otherwise)
        // and ground that is burning before its scar has built up.
        x = night * 0.92;                        col = lerp(col, float3(0.004, 0.005, 0.008), x); a = a + x - a * x;
        x = saturate(Fire.r * 2.0) * 0.85;       col = lerp(col, float3(0.035, 0.030, 0.028), x); a = a + x - a * x;
        x = pow(deposit, 0.7) * 0.35;            col = lerp(col, float3(0.25, 0.20, 0.16), x); a = a + x - a * x;
        x = saturate(Fire.g * 0.92);             col = lerp(col, float3(0.035, 0.030, 0.028), x); a = a + x - a * x;
        x = flood * 0.60;                        col = lerp(col, float3(0.10, 0.14, 0.18), x); a = a + x - a * x;
        x = scorch * 0.94;                       col = lerp(col, float3(0.020, 0.017, 0.015), x); a = a + x - a * x;
        x = saturate(crater.g * 1.3) * 0.80;     col = lerp(col, float3(0.34, 0.29, 0.25), x); a = a + x - a * x;
        x = crater.r * 0.45;                     col = lerp(col, float3(0.015, 0.013, 0.012), x); a = a + x - a * x;
        x = saturate(crest * 0.8 + flood * 0.3); col = lerp(col, float3(0.74, 0.95, 1.00), x); a = a + x - a * x;
        return float4(col, saturate(a) * Terrain.x);
    }

    // Magma veins and wildfire, as in earth_surface.hlsl.
    float3 mp = n * 150.0 + (float3(VNoise(n * 37.0), VNoise(n * 37.0 + 19.0), VNoise(n * 37.0 + 53.0)) - 0.5) * 7.0;
    float ridgeA = 1.0 - abs(2.0 * VNoise(mp) - 1.0);
    float ridgeB = 1.0 - abs(2.0 * VNoise(mp * 2.7 + 11.0) - 1.0);
    float veins = ridgeA * 0.62 + ridgeB * 0.38;
    float veinMask = smoothstep(0.95 - 0.55 * heat, 1.0, veins);
    float molten = saturate(veinMask * (0.35 + heat) + heat * heat * heat * 0.8) * saturate(Mask.r * 1.6);
    float3 magmaCol = lerp(float3(0.90, 0.06, 0.00), float3(1.00, 0.58, 0.14), saturate(molten * heat * 1.6));
    float3 emissive = magmaCol * molten * (1.5 + 11.0 * heat);

    float flames = VNoise(n * 520.0 + float3(0.0, 0.0, Time * 0.8)) * VNoise(n * 1400.0 - float3(Time * 1.1, 0.0, 0.0));
    float flicker = 0.75 + 0.25 * sin(Time * 9.0 + veins * 31.0);
    emissive += float3(1.00, 0.27, 0.04) * smoothstep(0.05, 0.6, Fire.r) * smoothstep(0.10, 0.45, flames) * flicker * 1.7;
    // City lights: streamed imagery is a daytime photograph, so the night side
    // gets the Black Marble lights back, broken into points as in low orbit, and
    // dead wherever the grid is down.
    float blackout = max(saturate(Mask.r * 4.0), saturate(Wave.b * 9.0));
    float sparkle = Hash13(floor(n * 14000.0));
    float3 lights = Texture2DSampleLevel(NightTex, NightTexSampler, uv, 0).rgb;
    emissive += lights * float3(1.0, 0.78, 0.45) * 4.2 * (0.08 + 4.5 * pow(sparkle, 5.0)) * 0.6 * night * (1.0 - blackout);
    return float4(emissive * Terrain.x, 0.0);
}
