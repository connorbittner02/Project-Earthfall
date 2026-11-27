// M_EarthPhotoreal. Unlit: every photon is accounted for here, so the look does
// not depend on SkyAtmosphere, sky light, exposure metering or shadow cascades.
// All vectors are planet-local. Tuning = (sun intensity, city-light gain,
// atmosphere health 0..1, sunglint gain).
float3 Main(float3 LocalPos, float3 NormalLocal, float3 ViewLocal, float CamDist,
            float3 DayTex, float3 NightTex, float3 NormalTex, float3 MasksTex,
            float3 Mask, float2 PomUV, Texture2D AshTex, SamplerState AshTexSampler,
            Texture2D CraterTex, SamplerState CraterTexSampler, float3 Wave, float3 Fire,
            float CountryId, float4 Inspect, float4 Pom, float3 Sun, float Time, float4 CloudLayer, float4 Tuning, float4 Climate,
            float4 A0, float4 B0, float4 C0, float4 A1, float4 B1, float4 C1,
            float4 A2, float4 B2, float4 C2, float4 A3, float4 B3, float4 C3)
{
    float3 n = normalize(LocalPos);
    float3 Ng = normalize(NormalLocal);
    float3 V = normalize(ViewLocal);
    // Left-handed world: east is -Y at the prime meridian.
    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);

    // --- Crater relief from the POM height field: bump the geometric normal by
    // the screen-space gradient of the height the view ray landed on.
    float3 Ash = Texture2DSampleLevel(AshTex, AshTexSampler, PomUV, 0).rgb;
    float2 crater = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV, 0).rg;
    float craterHeight = crater.g * Pom.y - crater.r * Pom.x;
    // Central differences three texels wide. Screen-space derivatives of a
    // bilinear-filtered height are constant per texel and light as facets.
    float2 craterTexel = float2(1.0 / 2048.0, 1.0 / 1024.0) * 1.5;
    float2 craterE = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV + float2(craterTexel.x, 0.0), 0).rg;
    float2 craterW = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV - float2(craterTexel.x, 0.0), 0).rg;
    float2 craterS = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV + float2(0.0, craterTexel.y), 0).rg;
    float2 craterN = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV - float2(0.0, craterTexel.y), 0).rg;
    float craterCosLat = max(sqrt(saturate(1.0 - n.z * n.z)), 0.15);
    // Height and distance are both in radians, so these are true slopes.
    float slopeEast = ((craterE.g - craterW.g) * Pom.y - (craterE.r - craterW.r) * Pom.x)
                    / (2.0 * craterTexel.x * 6.2831853 * craterCosLat);
    float slopeSouth = ((craterS.g - craterN.g) * Pom.y - (craterS.r - craterN.r) * Pom.x)
                     / (2.0 * craterTexel.y * 3.14159265);
    Ng = normalize(Ng - (east * slopeEast - north * slopeSouth) * 1.6);

    float water = MasksTex.r;
    float ice = MasksTex.b;
    // Nuclear winter. Climate.x pushes the frost line from the poles toward the
    // tropics; two noise scales make its edge a ragged front, not a parallel.
    float frostEdge = lerp(1.45, 0.42, Climate.x) + (VNoise(n * 9.0) - 0.5) * 0.22 + (VNoise(n * 40.0) - 0.5) * 0.06;
    float frost = Climate.x > 0.001 ? smoothstep(frostEdge, frostEdge + 0.10, abs(asin(clamp(n.z, -1.0, 1.0)))) : 0.0;
    ice = max(ice, frost);

    // Impact mask: R carries a 0.25 EMP plateau under a 1.0 scorch core.
    float blackout = saturate(Mask.r * 4.0);
    // Water does not char: the scorch core is a land effect only.
    float scorch = saturate((Mask.r - 0.25) / 0.60) * (1.0 - water);
    float heat = saturate(Mask.g);
    float cleared = saturate(Mask.b);
    float soot = pow(saturate(Ash.r), 0.7);
    float fallout = saturate(Ash.g);
    float deposit = saturate(Ash.b);

    // --- Normals: NASA relief on land, procedural swell on water. Tangent frame
    // is +X east, +Y south, matching prepare_earth_textures.py.
    float reliefFade = saturate((1400.0 - CamDist) / 900.0) * 0.5 + 0.5;
    float3 reliefN = normalize(Ng + (east * NormalTex.x - north * NormalTex.y) * 1.5 * reliefFade * (1.0 - scorch * 0.7));
    float swellA = VNoise(n * 90.0 + float3(Time * 0.05, 0.0, 0.0)) - 0.5;
    float swellB = VNoise(n * 260.0 - float3(0.0, Time * 0.08, 0.0)) - 0.5;
    float3 waterN = normalize(Ng + (east * swellA + north * swellB) * 0.030);
    float3 N = normalize(lerp(reliefN, waterN, water));

    // --- Low-orbit detail. The 8K maps run out near 5 km per texel, so as the
    // camera comes down procedural detail fades in over them. It fades out again
    // toward the horizon, where a pixel covers too much ground to resolve it.
    float nearDetail = (1.0 - smoothstep(6.0, 48.0, CamDist)) * saturate(dot(n, V) * 4.0);
    float landDetail = 0.5;
    float canopy = 0.0;
    float blocks = 0.5;
    // City lights resolve into points over a longer range than the ground detail.
    float nearCity = 1.0 - smoothstep(6.0, 60.0, CamDist);
    float sparkle = Hash13(floor(n * 14000.0));
    [branch] if (nearDetail > 0.001)
    {
        // Micro-terrain: the finite-difference gradient of an fbm, steeper where
        // the NASA relief says the ground is already rugged.
        // Bent through a slow warp so the noise lattice never shows as creases.
        float3 dp = n * 1100.0 + (float3(VNoise(n * 260.0), VNoise(n * 260.0 + 31.0), VNoise(n * 260.0 + 57.0)) - 0.5) * 2.4;
        float h0 = Fbm(dp, 3);
        float hE = Fbm(dp + east * 0.35, 3);
        float hN = Fbm(dp + north * 0.35, 3);
        landDetail = h0;
        float rugged = saturate(length(NormalTex.xy) * 6.0);
        float3 microTilt = clamp((east * (h0 - hE) + north * (h0 - hN)) / 0.35, -0.5, 0.5);
        N = normalize(N + microTilt * (0.10 + 0.35 * rugged) * nearDetail * (1.0 - water) * (1.0 - 0.6 * ice) * (1.0 - scorch));
        // Forest canopy clumps and irregular city blocks.
        canopy = saturate((VNoise(n * 5200.0) * 0.6 + VNoise(n * 13000.0) * 0.4 - 0.35) * 2.2);
        blocks = Hash13(floor(n * 7000.0));
        // Ocean: three finer wave trains under the orbital swell.
        float waveA = VNoise(n * 1400.0 + float3(Time * 0.35, 0.0, 0.0)) - 0.5;
        float waveB = VNoise(n * 3800.0 - float3(0.0, Time * 0.60, 0.0)) - 0.5;
        float waveC = VNoise(n * 9000.0 + float3(0.0, 0.0, Time * 0.90)) - 0.5;
        float waveD = VNoise(n * 9000.0 + float3(31.7, Time * 0.90, 0.0)) - 0.5;
        N = normalize(N + (east * (waveA + 0.6 * waveC) + north * (waveB + 0.6 * waveD)) * 0.10 * nearDetail * water);
    }

    // --- Sun. The terminator is softened by the atmosphere and the low sun is
    // reddened by its long slant path.
    float NdLs = dot(n, Sun);
    float terminator = smoothstep(-0.06, 0.16, NdLs);
    float NdL = saturate(dot(N, Sun));
    float day = smoothstep(-0.10, 0.22, NdLs);
    float3 sunCol = lerp(float3(1.0, 0.36, 0.10), float3(1.0, 0.97, 0.92), smoothstep(-0.02, 0.42, NdLs)) * Tuning.x;

    // --- Shadow marches along the sun vector: four steps across the crater
    // height field for rim shadows, three through the ash plume overhead.
    float cosLat = max(sqrt(saturate(1.0 - n.z * n.z)), 0.15);
    float3 sunT = float3(dot(Sun, east), dot(Sun, north), max(dot(Sun, n), 0.02));
    float2 sunUV = float2(sunT.x / (6.2831853 * cosLat), -sunT.y / 3.14159265);
    float craterShadow = 0.0;
    for (int cs = 1; cs <= 4; cs++)
    {
        float reach = cs * 0.006;
        float2 ahead = Texture2DSampleLevel(CraterTex, CraterTexSampler, PomUV + sunUV * reach, 0).rg;
        craterShadow = max(craterShadow, saturate((ahead.g * Pom.y - ahead.r * Pom.x - craterHeight - sunT.z * reach) * 500.0));
    }
    float plumeShadow = 0.0;
    for (int ps = 1; ps <= 3; ps++)
    {
        plumeShadow = max(plumeShadow, Texture2DSampleLevel(AshTex, AshTexSampler, PomUV + sunUV * (ps * 0.022), 0).r);
    }
    float sunShade = (1.0 - 0.75 * craterShadow) * (1.0 - 0.50 * pow(saturate(plumeShadow), 0.7));
    // Standing ash and steam columns throw long soft shadows across the ground.
    sunShade *= 1.0 - 0.60 * max(max(ColumnShadow(n, Sun, A0, C0), ColumnShadow(n, Sun, A1, C1)), max(ColumnShadow(n, Sun, A2, C2), ColumnShadow(n, Sun, A3, C3)));

    // --- Cloud shadow: the same weather field, sampled toward the sun.
    float3 sunTangent = Sun - n * NdLs;
    float3 shadowDir = normalize(n + sunTangent * 0.014);
    float ringUnused = 0.0;
    shadowDir = ShockDisplace(shadowDir, A0, B0, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A1, B1, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A2, B2, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A3, B3, ringUnused);
    float cloudCover = max(CloudField(shadowDir, Time, CloudLayer, 4),
                           0.7 * CloudField(normalize(shadowDir + sunTangent * 0.020), Time, CloudLayer, 3));
    float cloudShadow = 1.0 - 0.60 * cloudCover * (1.0 - cleared);

    // --- Albedo with destruction.
    float3 albedo = DayTex;
    // Low-orbit ground texture: mottled soil and rock, darker clumped canopy
    // over vegetation, and grey block structure where the night map says city.
    float greenness = saturate((DayTex.g - max(DayTex.r, DayTex.b) * 0.80) * 9.0);
    float urban = saturate((dot(NightTex, float3(0.4, 0.4, 0.2)) - 0.12) * 3.0) * (1.0 - water);
    float landNear = nearDetail * (1.0 - water) * (1.0 - ice);
    albedo *= 1.0 + (landDetail - 0.5) * 0.55 * landNear;
    albedo = lerp(albedo, albedo * float3(0.62, 0.80, 0.58), canopy * greenness * landNear * 0.7);
    albedo = lerp(albedo, float3(0.36, 0.35, 0.34) * (0.8 + 0.4 * blocks), urban * landNear * 0.40);
    // Sea ice is whiter than frosted land, which keeps a little of its ground.
    albedo = lerp(albedo, float3(0.80, 0.86, 0.92), frost * lerp(0.72, 0.92, water));
    // Wildfire scars: charred basalt and ash where the front has passed.
    albedo = lerp(albedo, float3(0.035, 0.030, 0.028), saturate(Fire.g * 0.92) * (1.0 - water));
    // Tsunami run-up leaves the shore dark and wet.
    float flood = saturate(Wave.b * 3.0) * (1.0 - water);
    albedo = lerp(albedo, albedo * float3(0.34, 0.42, 0.50), flood);
    blackout = max(blackout, saturate(Wave.b * 9.0));
    albedo = lerp(albedo, albedo * float3(0.42, 0.33, 0.26), pow(deposit, 0.7));
    albedo = lerp(albedo, float3(0.020, 0.017, 0.015), scorch * 0.94);
    // Pale ejecta on the raised rim and rays, and occlusion down in the bowl.
    albedo = lerp(albedo, float3(0.34, 0.29, 0.25), saturate(crater.g * 1.3) * (1.0 - water));
    albedo *= 1.0 - 0.45 * crater.r;
    float airborne = 1.0 - 0.70 * soot;

    float3 lit = albedo * (sunCol * NdL * terminator * cloudShadow * airborne * sunShade
                           + float3(0.10, 0.16, 0.30) * 0.10 * day);

    // --- Ocean: GGX sunglint at roughness ~0.30 with wind-patch variation, plus
    // a grazing-angle sky reflection.
    float calm = (1.0 - scorch) * water * (1.0 - ice);
    float3 H = normalize(Sun + V);
    float NdH = saturate(dot(N, H));
    float NdV = saturate(dot(N, V));
    float VdH = saturate(dot(V, H));
    float rough = lerp(0.27, 0.40, VNoise(n * 14.0 + float3(Time * 0.01, 0.0, 0.0)));
    float a2 = Pow2(rough * rough);
    float denom = NdH * NdH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * denom * denom);
    float F = 0.02 + 0.98 * pow(1.0 - VdH, 5.0);
    float glint = D * F * 0.25 / max(0.10, max(NdL, NdV));
    lit += sunCol * glint * NdL * terminator * calm * Pow2(cloudShadow) * airborne * Tuning.w;
    lit += float3(0.20, 0.38, 0.75) * pow(1.0 - saturate(dot(n, V)), 5.0) * 0.30 * day * calm;

    // --- Tsunami: white-cyan crests on open water and foam on the flooded shore.
    // Only the steep leading front foams; the sloshing sea behind it stays dark.
    // Foam follows the wave's velocity, not its height: a 2D wave leaves a long
    // slow tail behind the front, and only the fast-moving front should break.
    float waveSpeed = (Wave.g - 0.5) * 20.0;
    float crest = smoothstep(0.10, 0.60, abs(waveSpeed)) * water;
    float3 foam = float3(0.74, 0.95, 1.00);
    lit += foam * crest * (0.10 + 0.70 * day);
    lit += foam * flood * (0.02 + 0.08 * day);

    // --- Explosion flashes.
    float3 flash = ShockFlash(n, A0, B0, C0) + ShockFlash(n, A1, B1, C1)
                 + ShockFlash(n, A2, B2, C2) + ShockFlash(n, A3, B3, C3);
    lit += (albedo + 0.03) * flash;

    // --- Atmosphere seen against the surface: a thin saturated blue veil that
    // thickens to a crisp rim, turning orange through the twilight band.
    float mu = saturate(dot(n, V));
    float fresnel = pow(1.0 - mu, 4.0);
    float rim = pow(1.0 - mu, 9.0);
    float atmLight = smoothstep(-0.22, 0.28, NdLs);
    float twilight = exp(-Pow2(NdLs / 0.17));
    float3 rayleigh = lerp(float3(0.72, 0.24, 0.07), float3(0.15, 0.40, 1.00), smoothstep(0.35, 0.92, Tuning.z));
    float3 scatterCol = lerp(rayleigh, float3(1.00, 0.40, 0.10), twilight * 0.78);
    lit = lit * (1.0 - 0.40 * fresnel);
    lit += scatterCol * (0.030 + 0.75 * fresnel + 0.55 * rim) * atmLight;

    // --- Night side: city lights only, dead under scorch and EMP.
    float night = 1.0 - smoothstep(-0.20, 0.04, NdLs);
    // Sodium-vapour gold, as city webs look from the ISS.
    // Up close the glow breaks into individual blocks of light.
    float cityDetail = lerp(1.0, (0.08 + 4.5 * pow(sparkle, 5.0)) * 0.6, nearCity);
    float3 emissive = NightTex * cityDetail * float3(1.0, 0.78, 0.45) * 1.30 * night * (1.0 - blackout) * Tuning.y * (1.0 - 0.7 * soot);

    // --- Magma: ridged-noise veins that narrow and dim as the crater cools.
    // Warp the lookup so the veins meander instead of following the noise lattice.
    float3 mp = n * 150.0 + (float3(VNoise(n * 37.0), VNoise(n * 37.0 + 19.0), VNoise(n * 37.0 + 53.0)) - 0.5) * 7.0;
    float ridgeA = 1.0 - abs(2.0 * VNoise(mp) - 1.0);
    float ridgeB = 1.0 - abs(2.0 * VNoise(mp * 2.7 + 11.0) - 1.0);
    float veins = ridgeA * 0.62 + ridgeB * 0.38;
    float veinMask = smoothstep(0.95 - 0.55 * heat, 1.0, veins);
    // Open water quenches most of the melt.
    float molten = saturate(veinMask * (0.35 + heat) + heat * heat * heat * 0.8) * saturate(Mask.r * 1.6) * (1.0 - water);
    float3 magmaCol = lerp(float3(0.90, 0.06, 0.00), float3(1.00, 0.58, 0.14), saturate(molten * heat * 1.6));
    emissive += magmaCol * molten * (1.5 + 11.0 * heat);
    float ember = smoothstep(0.86, 1.0, veins) * scorch * (0.6 + 0.4 * sin(Time * 1.7 + veins * 20.0));
    emissive += float3(0.85, 0.10, 0.01) * ember * 0.40;

    // --- Wildfire: a flickering front, visible by day and vivid at night.
    // The automaton runs on a coarse grid, so the glow is broken up by fine
    // noise into ragged fronts and embers rather than lit texels.
    float flames = VNoise(n * 520.0 + float3(0.0, 0.0, Time * 0.8)) * VNoise(n * 1400.0 - float3(Time * 1.1, 0.0, 0.0));
    float flicker = 0.75 + 0.25 * sin(Time * 9.0 + veins * 31.0);
    emissive += float3(1.00, 0.27, 0.04) * smoothstep(0.05, 0.6, Fire.r) * smoothstep(0.10, 0.45, flames) * flicker * 1.7 * (1.0 - water);

    // --- Fallout: a faint sickly shimmer where radioactive ash is settling.
    emissive += float3(0.30, 0.55, 0.06) * fallout * 0.05 * (0.5 + 0.5 * night);

    // --- Country Dossier highlight: a faint fill and a crisp border on the
    // inspected nation. The ID texture is nearest-filtered, so fwidth() of the
    // match is exactly the border.
    float inspected = (1.0 - saturate(abs(CountryId * 255.0 - Inspect.x) * 2.0)) * Inspect.y;
    float border = saturate(fwidth(inspected) * 1.5);
    emissive += float3(0.30, 0.82, 1.00) * (inspected * 0.07 + border * 0.9) * Inspect.y;

    return lit + emissive;
}
