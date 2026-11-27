// M_TerrainDestruction. A planet-sized deferred decal that paints the destruction
// simulations onto streamed terrain. The pixel's position is turned back into
// longitude and latitude and used to sample the same equirectangular render
// targets the orbital surface shader reads, so a crater seen from orbit is the
// same crater on the ground.
//   RelPos = pixel position relative to the camera, CamPos = camera position in
//   planet-local space. Terrain.x = hand-over fade 0..1.
//   Mode 0 returns (base colour, opacity); Mode 1 returns (emissive, 0);
//   Mode 2 returns (roughness, 0, 0, 0).
float4 Main(float3 RelPos, float3 CamPos,
            Texture2D MaskTex, SamplerState MaskTexSampler,
            Texture2D AshTex, SamplerState AshTexSampler,
            Texture2D CraterTex, SamplerState CraterTexSampler,
            Texture2D WaveTex, SamplerState WaveTexSampler,
            Texture2D FireTex, SamplerState FireTexSampler,
            Texture2D NightTex, SamplerState NightTexSampler,
            Texture2D MasksTex, SamplerState MasksTexSampler,
            Texture2D PlagueTex, SamplerState PlagueTexSampler,
            float3 Sun, float Time, float4 Terrain, float4 Atmos, float4 Column, float Mode)
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
    float crest = smoothstep(0.22, 0.95, abs((Wave.g - 0.5) * 20.0));
    float night = 1.0 - smoothstep(-0.20, 0.04, dot(n, Sun));

    // Open water, from the orbital water mask. That mask is about 5 km per texel,
    // so only water well clear of the coast is treated; the shallows keep the
    // streamed imagery's own colour.
    float3 masks = Texture2DSampleLevel(MasksTex, MasksTexSampler, uv, 0).rgb;
    float water = smoothstep(0.80, 0.98, masks.r) * (1.0 - masks.b);
    // Water does not char.
    scorch *= 1.0 - smoothstep(0.35, 0.65, masks.r);

    // Crater relief as shading: the slope of the crater height field against the
    // sun, exactly as earth_surface.hlsl bends its normal. Pom = (0.050, 0.020).
    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);
    float2 texel = float2(1.0 / 2048.0, 1.0 / 1024.0) * 1.5;
    float2 cE = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv + float2(texel.x, 0.0), 0).rg;
    float2 cW = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv - float2(texel.x, 0.0), 0).rg;
    float2 cS = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv + float2(0.0, texel.y), 0).rg;
    float2 cN = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv - float2(0.0, texel.y), 0).rg;
    float cosLat = max(sqrt(saturate(1.0 - n.z * n.z)), 0.15);
    float slopeEast = ((cE.g - cW.g) * 0.020 - (cE.r - cW.r) * 0.050) / (2.0 * texel.x * 6.2831853 * cosLat);
    float slopeSouth = ((cS.g - cN.g) * 0.020 - (cS.r - cN.r) * 0.050) / (2.0 * texel.y * 3.14159265);
    float relief = clamp(-(dot(Sun, east) * slopeEast - dot(Sun, north) * slopeSouth) * 1.6, -1.0, 1.0);

    // Aerial haze: the further the ground, the more air in front of it. The
    // scale height grows with the camera's altitude, and the colour follows the
    // same soot-reddened sky as the orbital limb. Atmos = (health, sunlight).
    float hazeAltitude = max(length(CamPos) - 99.85, 0.05);
    float haze = (1.0 - exp(-length(RelPos) / (4.0 + 9.0 * hazeAltitude))) * 0.75;
    float3 hazeCol = lerp(float3(0.62, 0.24, 0.09), float3(0.40, 0.58, 0.95), smoothstep(0.35, 0.92, Atmos.x)) * 0.55;

    // Bio mask: R = infection, G = rage or insurgency. Up close the plague is a
    // sickly ground haze and the fighting is fires a few kilometres across under
    // drifting smoke.
    float2 Bio = Texture2DSampleLevel(PlagueTex, PlagueTexSampler, uv, 0).rg;
    float infection = saturate(Bio.r);
    float rage = saturate(Bio.g);
    float bioHaze = smoothstep(0.02, 0.5, infection) * (0.45 + 0.55 * (VNoise(n * 170.0 + float3(Time * 0.05, 0.0, Time * 0.03)) * 0.5 + VNoise(n * 900.0 - float3(0.0, Time * 0.07, 0.0)) * 0.5));
    // Fires: small cores a kilometre or two across, broken up by a fast flame
    // noise so they flicker and never read as flat discs.
    float fireField = VNoise(n * 3400.0 + float3(0.0, 0.0, floor(Time * 0.35) * 7.0));
    float rageFlames = 0.45 + 0.55 * VNoise(n * 26000.0 + float3(Time * 3.0, 0.0, -Time * 2.0));
    float hotspot = rage * smoothstep(0.74, 0.90, fireField);
    float fireCore = rage * smoothstep(0.84, 0.95, fireField);
    float rageSmoke = rage * smoothstep(0.35, 0.80, VNoise(n * 700.0 + float3(Time * 0.04, 0.0, 0.0)));

    if (Mode < 0.5 || Mode > 1.5)
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
        x = saturate(Fire.g * 0.92) * 0.80;      col = lerp(col, float3(0.035, 0.030, 0.028), x); a = a + x - a * x;
        // Inundated ground: dark, wet mud and standing water, not foam.
        // The flood mask is about 39 km per texel and its coast sits inland of the
        // streamed one, so the wet band is widened by taking the strongest of five
        // taps a texel and a half apart; that carries it down to the real shore.
        // (Deciding it from the tiles' own height was tried and backed out: coarse
        // distant tiles sag below sea level and flooded whole hill ranges.)
        float2 floodTexel = float2(1.5 / 1024.0, 1.5 / 512.0);
        float floodNear = max(max(Texture2DSampleLevel(WaveTex, WaveTexSampler, uv + float2(floodTexel.x, 0.0), 0).b,
                                  Texture2DSampleLevel(WaveTex, WaveTexSampler, uv - float2(floodTexel.x, 0.0), 0).b),
                              max(Texture2DSampleLevel(WaveTex, WaveTexSampler, uv + float2(0.0, floodTexel.y), 0).b,
                                  Texture2DSampleLevel(WaveTex, WaveTexSampler, uv - float2(0.0, floodTexel.y), 0).b));
        floodNear = saturate(max(floodNear, Wave.b) * 3.0);
        // Only land floods; the open sea keeps its own shading.
        float wet = smoothstep(0.04, 0.35, floodNear) * 0.80 * (1.0 - water);
        x = wet;                                 col = lerp(col, float3(0.034, 0.036, 0.038), x); a = a + x - a * x;
        x = scorch * 0.94;                       col = lerp(col, float3(0.020, 0.017, 0.015), x); a = a + x - a * x;
        x = saturate(crater.g * 1.3) * 0.40;     col = lerp(col, float3(0.22, 0.19, 0.16), x); a = a + x - a * x;
        x = crater.r * 0.45;                     col = lerp(col, float3(0.015, 0.013, 0.012), x); a = a + x - a * x;
        // Sunlit and shadowed crater slopes.
        x = saturate(-relief) * 0.70;            col = lerp(col, float3(0.010, 0.009, 0.008), x); a = a + x - a * x;
        x = saturate(relief) * 0.35;             col = lerp(col, float3(0.50, 0.45, 0.40), x); a = a + x - a * x;
        // Deep ocean blue under the glint added in the emissive pass.
        x = water * 0.80;                        col = lerp(col, float3(0.010, 0.040, 0.105), x); a = a + x - a * x;
        x = saturate(crest * 0.7);               col = lerp(col, float3(0.74, 0.95, 1.00), x); a = a + x - a * x;
        x = bioHaze * 0.66;                      col = lerp(col, float3(0.19, 0.36, 0.05), x); a = a + x - a * x;
        x = rageSmoke * 0.68;                    col = lerp(col, float3(0.030, 0.026, 0.024), x); a = a + x - a * x;
        x = hotspot * 0.95;                      col = lerp(col, float3(0.012, 0.008, 0.006), x); a = a + x - a * x;
        x = ColumnShadow(n, Sun, Column, float4(0.0, 0.0, 0.0, Column.w)) * 0.55;
                                                 col = lerp(col, float3(0.0, 0.0, 0.0), x); a = a + x - a * x;
        // Nuclear winter. Climate.x pushes the frost line from the poles toward the
        // tropics; two noise scales make its edge a ragged front, not a parallel.
        float frostEdge = lerp(1.45, 0.42, Atmos.z) + (VNoise(n * 9.0) - 0.5) * 0.22 + (VNoise(n * 40.0) - 0.5) * 0.06;
        float frost = Atmos.z > 0.001 ? smoothstep(frostEdge, frostEdge + 0.10, abs(asin(clamp(n.z, -1.0, 1.0)))) : 0.0;
        x = frost * 0.78;                        col = lerp(col, float3(0.80, 0.86, 0.92), x); a = a + x - a * x;
        x = haze;                                col = lerp(col, hazeCol, x); a = a + x - a * x;
        // Roughness is blended by the same opacity as colour, so it is glossy only
        // in proportion to how much of that opacity is flood water. 0.15 and 0.28 were
        // tried: seen at a grazing angle, ground that glossy mirrors the warm horizon
        // and reads as bright sand dunes. 0.55 is a damp sheen that stays dark.
        if (Mode > 1.5) return float4(lerp(0.90, 0.55, saturate(wet * (1.0 - haze) / max(a, 1e-3))), 0.0, 0.0, 0.0);
        return float4(col, saturate(a) * Terrain.x);
    }

    // Magma veins and wildfire, as in earth_surface.hlsl.
    float3 mp = n * 150.0 + (float3(VNoise(n * 37.0), VNoise(n * 37.0 + 19.0), VNoise(n * 37.0 + 53.0)) - 0.5) * 7.0;
    float ridgeA = 1.0 - abs(2.0 * VNoise(mp) - 1.0);
    float ridgeB = 1.0 - abs(2.0 * VNoise(mp * 2.7 + 11.0) - 1.0);
    float veins = ridgeA * 0.62 + ridgeB * 0.38;
    float veinMask = smoothstep(0.95 - 0.55 * heat, 1.0, veins);
    // Open water quenches most of the melt, as on the orbital surface.
    float molten = saturate(veinMask * (0.35 + heat) + heat * heat * heat * 0.8) * saturate(Mask.r * 1.6) * (1.0 - smoothstep(0.35, 0.65, masks.r));
    float3 magmaCol = lerp(float3(0.90, 0.06, 0.00), float3(1.00, 0.58, 0.14), saturate(molten * heat * 1.6));
    float3 emissive = magmaCol * molten * (1.5 + 11.0 * heat);

    float flames = VNoise(n * 520.0 + float3(0.0, 0.0, Time * 0.8)) * VNoise(n * 1400.0 - float3(Time * 1.1, 0.0, 0.0));
    float flicker = 0.75 + 0.25 * sin(Time * 9.0 + veins * 31.0);
    emissive += float3(1.00, 0.27, 0.04) * smoothstep(0.05, 0.6, Fire.r) * smoothstep(0.10, 0.45, flames) * flicker * 1.7;
    // Ocean: the orbital shader's GGX sunglint and grazing sky reflection, on
    // procedural swell with three finer wave trains.
    float3 V = normalize(-RelPos);
    float NdLs = dot(n, Sun);
    float day = smoothstep(-0.10, 0.22, NdLs);
    float3 sunCol = lerp(float3(1.0, 0.36, 0.10), float3(1.0, 0.97, 0.92), smoothstep(-0.02, 0.42, NdLs)) * 1.75;
    float3 waveN = normalize(n
        + (east * (VNoise(n * 90.0 + float3(Time * 0.05, 0.0, 0.0)) - 0.5)
         + north * (VNoise(n * 260.0 - float3(0.0, Time * 0.08, 0.0)) - 0.5)) * 0.030
        + (east * (VNoise(n * 1400.0 + float3(Time * 0.35, 0.0, 0.0)) - 0.5 + 0.6 * (VNoise(n * 9000.0 + float3(0.0, 0.0, Time * 0.90)) - 0.5))
         + north * (VNoise(n * 3800.0 - float3(0.0, Time * 0.60, 0.0)) - 0.5 + 0.6 * (VNoise(n * 9000.0 + float3(31.7, Time * 0.90, 0.0)) - 0.5))) * 0.10);
    float3 H = normalize(Sun + V);
    float NdH = saturate(dot(waveN, H));
    float NdV = saturate(dot(waveN, V));
    float NdL = saturate(dot(waveN, Sun));
    float rough = lerp(0.27, 0.40, VNoise(n * 14.0 + float3(Time * 0.01, 0.0, 0.0)));
    float a2 = Pow2(rough * rough);
    float denom = NdH * NdH * (a2 - 1.0) + 1.0;
    float glint = a2 / (3.14159265 * denom * denom) * (0.02 + 0.98 * pow(1.0 - saturate(dot(V, H)), 5.0)) * 0.25 / max(0.10, max(NdL, NdV));
    emissive += (sunCol * glint * NdL * 2.6 + float3(0.20, 0.38, 0.75) * pow(1.0 - saturate(dot(n, V)), 5.0) * 0.30) * day * water;

    // Bio-cloud glow, and fires where the fighting is.
    emissive += lerp(float3(0.42, 0.16, 0.55), float3(0.30, 0.62, 0.10), smoothstep(0.10, 0.45, infection)) * bioHaze * 0.40;
    emissive += (float3(1.00, 0.22, 0.03) * hotspot * 3.0 + float3(1.00, 0.72, 0.25) * fireCore * 9.0) * rageFlames;

    // City lights: streamed imagery is a daytime photograph, so the night side
    // gets the Black Marble lights back, broken into points as in low orbit, and
    // dead wherever the grid is down.
    float blackout = max(saturate(Mask.r * 4.0), saturate(Wave.b * 9.0));
    float sparkle = Hash13(floor(n * 14000.0));
    float3 lights = Texture2DSampleLevel(NightTex, NightTexSampler, uv, 0).rgb;
    emissive += lights * float3(1.0, 0.78, 0.45) * 4.2 * (0.08 + 4.5 * pow(sparkle, 5.0)) * 0.6 * night * (1.0 - blackout);
    return float4(emissive * (1.0 - 0.8 * haze) * Terrain.x, 0.0);
}
