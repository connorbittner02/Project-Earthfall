// M_EarthClouds. Unlit translucent shell; returns (colour, opacity). Clear sky is
// exactly 0 opacity, so the layer can never film the oceans. Layer = (seed,
// threshold, zonal stretch, drift). Look = (opacity scale, self-shadow strength,
// ash visibility, sun intensity).
float4 Main(float3 LocalPos, float3 ViewLocal, float CamDist, float2 UV, float3 Mask, Texture2D AshTex, SamplerState AshTexSampler, float3 Sun, float Time,
            float4 Layer, float4 Look,
            float4 A0, float4 B0, float4 C0, float4 A1, float4 B1, float4 C1,
            float4 A2, float4 B2, float4 C2, float4 A3, float4 B3, float4 C3)
{
    float3 n = normalize(LocalPos);
    float3 Ash = Texture2DSampleLevel(AshTex, AshTexSampler, UV, 0).rgb;

    // Shockwaves shove the weather outward and leave a condensation front.
    float ring = 0.0;
    float3 ns = ShockDisplace(n, A0, B0, ring);
    ns = ShockDisplace(ns, A1, B1, ring);
    ns = ShockDisplace(ns, A2, B2, ring);
    ns = ShockDisplace(ns, A3, B3, ring);
    ring = saturate(ring);

    float cleared = saturate(Mask.b);
    float density = CloudField(ns, Time, Layer, 6);
    // High-frequency wispy erosion: thin cloud is eaten away first, so decks
    // fray into filaments instead of ending on a solid white edge.
    // Value noise shows its lattice as straight creases up close, so the detail
    // lookups are bent through a slow vector warp first.
    float3 warp = float3(VNoise(ns * 17.0), VNoise(ns * 17.0 + 31.0), VNoise(ns * 17.0 + 57.0)) - 0.5;
    float3 wp = ns * 46.0 + warp * 1.6 + float3(Time * 0.012, 0.0, Time * 0.007);
    float wisps = Fbm(wp, 3) * 0.7 + Fbm(wp * 3.1 + 5.0, 2) * 0.3;
    density = saturate(density - smoothstep(0.30, 0.62, wisps) * (1.0 - density * 0.55) * 0.60);
    // Low orbit: a finer erosion octave keeps cloud edges crisp when the camera
    // skims the deck.
    float nearDetail = 1.0 - smoothstep(4.0, 40.0, CamDist);
    [branch] if (nearDetail > 0.001)
    {
        float fine = Fbm(ns * 420.0 + warp * 9.0 + (wisps - 0.5) * 3.0 + float3(Time * 0.02, 0.0, 0.0), 3);
        density = saturate(density - smoothstep(0.35, 0.75, fine) * (1.0 - density * 0.6) * 0.45 * nearDetail);
    }
    density = saturate(density + ring * 0.28) * (1.0 - cleared);

    // Volumetric-style relief: march one step toward the sun through the same
    // field, so sunward edges glow and lee sides fall into shadow.
    float NdL = dot(n, Sun);
    float3 sunTangent = Sun - n * NdL;
    // The thin high deck (Look.y < 1) cannot shadow itself visibly, so it skips
    // the second weather lookup and shades from its own density.
    float towardSun = density;
    if (Look.y >= 1.0)
    {
        // Three steps toward the sun: near steps carve relief into each cloud,
        // the far step lets storm walls shadow the deck behind them.
        towardSun = (CloudField(normalize(ns + sunTangent * 0.012), Time, Layer, 3)
                   + CloudField(normalize(ns + sunTangent * 0.026), Time, Layer, 3)
                   + CloudField(normalize(ns + sunTangent * 0.045), Time, Layer, 3)) * 0.55 * (1.0 - cleared);
    }
    float shade = exp(-towardSun * Look.y);

    float day = smoothstep(-0.12, 0.20, NdL);
    float3 sunCol = lerp(float3(1.0, 0.40, 0.14), float3(1.0, 0.98, 0.95), smoothstep(-0.02, 0.38, NdL)) * Look.w;
    float3 cloudCol = 0.86 * (sunCol * (0.28 + 0.80 * shade) * day
                              + float3(0.10, 0.16, 0.28) * 0.55 * day
                              + float3(0.004, 0.006, 0.012));
    cloudCol *= lerp(0.80, 1.0, density);

    // Towering ash plumes shadow the cloud deck: three samples toward the sun.
    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);
    float cosLat = max(sqrt(saturate(1.0 - n.z * n.z)), 0.15);
    float2 sunUV = float2(dot(Sun, east) / (6.2831853 * cosLat), -dot(Sun, north) / 3.14159265);
    float plumeShadow = 0.0;
    for (int ps = 1; ps <= 3; ps++)
    {
        plumeShadow = max(plumeShadow, Texture2DSampleLevel(AshTex, AshTexSampler, UV + sunUV * (ps * 0.016), 0).r);
    }
    cloudCol *= 1.0 - 0.55 * pow(saturate(plumeShadow), 0.7);
    // And the columns that punch through the deck shadow it too.
    cloudCol *= 1.0 - 0.50 * max(max(ColumnShadow(n, Sun, A0, C0), ColumnShadow(n, Sun, A1, C1)), max(ColumnShadow(n, Sun, A2, C2), ColumnShadow(n, Sun, A3, C3)));
    // Semi-transparent vapour: feathered edges ramp to 0.45, and even the
    // thickest cores stop at 0.78 so ocean and land read through.
    float cloudAlpha = 0.45 * smoothstep(0.0, 0.50, density) + 0.33 * smoothstep(0.45, 1.0, density);

    // Soot rides above the weather and is lit the same way; fallout tints it.
    // A 0.7 power lifts thin plumes so 1x fallout reads clearly from orbit.
    float soot = pow(saturate(Ash.r), 0.7);
    float fallout = saturate(Ash.g);
    float sootAlpha = saturate(soot * 1.6) * 0.92 * Look.z;
    float3 sootCol = float3(0.150, 0.098, 0.060) * (0.10 + 0.90 * day) * Look.w
                   + float3(0.30, 0.55, 0.06) * fallout * 0.07;

    float alpha = sootAlpha + cloudAlpha * (1.0 - sootAlpha);
    float3 col = (sootCol * sootAlpha + cloudCol * cloudAlpha * (1.0 - sootAlpha)) / max(alpha, 1e-4);

    // Explosions light the cloud deck from below.
    float3 flash = ShockFlash(n, A0, B0, C0) + ShockFlash(n, A1, B1, C1)
                 + ShockFlash(n, A2, B2, C2) + ShockFlash(n, A3, B3, C3);
    col += flash * 0.45;

    // The shock front itself glows even over clear sky.
    float ringGlow = ring * max(max(B0.w, B1.w), max(B2.w, B3.w));
    col += float3(1.0, 0.90, 0.74) * ringGlow * 0.40;
    alpha = max(alpha, ringGlow * 0.16 * (1.0 - cleared * 0.5));

    // The shell is a faceted mesh: fade it out over the last few degrees before
    // its own horizon, where the facets would otherwise show as slivers.
    alpha *= smoothstep(0.03, 0.18, dot(n, normalize(ViewLocal)));
    return float4(col, saturate(alpha * Look.x));
}
