// M_EarthPhotoreal. Unlit: every photon is accounted for here, so the look does
// not depend on SkyAtmosphere, sky light, exposure metering or shadow cascades.
// All vectors are planet-local. Tuning = (sun intensity, city-light gain,
// atmosphere health 0..1, sunglint gain).
float3 Main(float3 LocalPos, float3 NormalLocal, float3 ViewLocal, float CamDist,
            float3 DayTex, float3 NightTex, float3 NormalTex, float3 MasksTex,
            float3 Mask, float3 Ash, float CountryId, float4 Inspect, float3 Sun, float Time, float4 CloudLayer, float4 Tuning,
            float4 A0, float4 B0, float4 C0, float4 A1, float4 B1, float4 C1,
            float4 A2, float4 B2, float4 C2, float4 A3, float4 B3, float4 C3)
{
    float3 n = normalize(LocalPos);
    float3 Ng = normalize(NormalLocal);
    float3 V = normalize(ViewLocal);
    // Left-handed world: east is -Y at the prime meridian.
    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);

    float water = MasksTex.r;
    float ice = MasksTex.b;

    // Impact mask: R carries a 0.25 EMP plateau under a 1.0 scorch core.
    float blackout = saturate(Mask.r * 4.0);
    float scorch = saturate((Mask.r - 0.25) / 0.60);
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

    // --- Sun. The terminator is softened by the atmosphere and the low sun is
    // reddened by its long slant path.
    float NdLs = dot(n, Sun);
    float terminator = smoothstep(-0.06, 0.16, NdLs);
    float NdL = saturate(dot(N, Sun));
    float day = smoothstep(-0.10, 0.22, NdLs);
    float3 sunCol = lerp(float3(1.0, 0.36, 0.10), float3(1.0, 0.97, 0.92), smoothstep(-0.02, 0.42, NdLs)) * Tuning.x;

    // --- Cloud shadow: the same weather field, sampled toward the sun.
    float3 sunTangent = Sun - n * NdLs;
    float3 shadowDir = normalize(n + sunTangent * 0.014);
    float ringUnused = 0.0;
    shadowDir = ShockDisplace(shadowDir, A0, B0, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A1, B1, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A2, B2, ringUnused);
    shadowDir = ShockDisplace(shadowDir, A3, B3, ringUnused);
    float cloudShadow = 1.0 - 0.60 * CloudField(shadowDir, Time, CloudLayer, 4) * (1.0 - cleared);

    // --- Albedo with destruction.
    float3 albedo = DayTex;
    albedo = lerp(albedo, albedo * float3(0.42, 0.33, 0.26), pow(deposit, 0.7));
    albedo = lerp(albedo, float3(0.020, 0.017, 0.015), scorch * 0.94);
    float airborne = 1.0 - 0.70 * soot;

    float3 lit = albedo * (sunCol * NdL * terminator * cloudShadow * airborne
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
    float3 rayleigh = lerp(float3(0.42, 0.30, 0.20), float3(0.15, 0.40, 1.00), Tuning.z);
    float3 scatterCol = lerp(rayleigh, float3(1.00, 0.40, 0.10), twilight * 0.78);
    lit = lit * (1.0 - 0.40 * fresnel);
    lit += scatterCol * (0.030 + 0.75 * fresnel + 0.55 * rim) * atmLight;

    // --- Night side: city lights only, dead under scorch and EMP.
    float night = 1.0 - smoothstep(-0.20, 0.04, NdLs);
    // Sodium-vapour gold, as city webs look from the ISS.
    float3 emissive = NightTex * float3(1.0, 0.78, 0.45) * 1.30 * night * (1.0 - blackout) * Tuning.y * (1.0 - 0.7 * soot);

    // --- Magma: ridged-noise veins that narrow and dim as the crater cools.
    // Warp the lookup so the veins meander instead of following the noise lattice.
    float3 mp = n * 150.0 + (float3(VNoise(n * 37.0), VNoise(n * 37.0 + 19.0), VNoise(n * 37.0 + 53.0)) - 0.5) * 7.0;
    float ridgeA = 1.0 - abs(2.0 * VNoise(mp) - 1.0);
    float ridgeB = 1.0 - abs(2.0 * VNoise(mp * 2.7 + 11.0) - 1.0);
    float veins = ridgeA * 0.62 + ridgeB * 0.38;
    float veinMask = smoothstep(0.95 - 0.55 * heat, 1.0, veins);
    float molten = saturate(veinMask * (0.35 + heat) + heat * heat * heat * 0.8) * saturate(Mask.r * 1.6);
    float3 magmaCol = lerp(float3(0.90, 0.06, 0.00), float3(1.00, 0.58, 0.14), saturate(molten * heat * 1.6));
    emissive += magmaCol * molten * (1.5 + 11.0 * heat);
    float ember = smoothstep(0.86, 1.0, veins) * scorch * (0.6 + 0.4 * sin(Time * 1.7 + veins * 20.0));
    emissive += float3(0.85, 0.10, 0.01) * ember * 0.40;

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
