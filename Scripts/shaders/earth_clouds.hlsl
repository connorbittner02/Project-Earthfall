// M_EarthClouds. Unlit translucent shell; returns (colour, opacity). Clear sky is
// exactly 0 opacity, so the layer can never film the oceans. Layer = (seed,
// threshold, zonal stretch, drift). Look = (opacity scale, self-shadow strength,
// ash visibility, sun intensity).
float4 Main(float3 LocalPos, float3 Mask, float3 Ash, float3 Sun, float Time,
            float4 Layer, float4 Look,
            float4 A0, float4 B0, float4 C0, float4 A1, float4 B1, float4 C1,
            float4 A2, float4 B2, float4 C2, float4 A3, float4 B3, float4 C3)
{
    float3 n = normalize(LocalPos);

    // Shockwaves shove the weather outward and leave a condensation front.
    float ring = 0.0;
    float3 ns = ShockDisplace(n, A0, B0, ring);
    ns = ShockDisplace(ns, A1, B1, ring);
    ns = ShockDisplace(ns, A2, B2, ring);
    ns = ShockDisplace(ns, A3, B3, ring);
    ring = saturate(ring);

    float cleared = saturate(Mask.b);
    float density = CloudField(ns, Time, Layer, 6);
    density = saturate(density + ring * 0.28) * (1.0 - cleared);

    // Volumetric-style relief: march one step toward the sun through the same
    // field, so sunward edges glow and lee sides fall into shadow.
    float NdL = dot(n, Sun);
    float3 sunTangent = Sun - n * NdL;
    float towardSun = CloudField(normalize(ns + sunTangent * 0.020), Time, Layer, 4) * (1.0 - cleared);
    float shade = exp(-towardSun * Look.y);

    float day = smoothstep(-0.12, 0.20, NdL);
    float3 sunCol = lerp(float3(1.0, 0.40, 0.14), float3(1.0, 0.98, 0.95), smoothstep(-0.02, 0.38, NdL)) * Look.w;
    float3 cloudCol = 0.86 * (sunCol * (0.28 + 0.80 * shade) * day
                              + float3(0.10, 0.16, 0.28) * 0.55 * day
                              + float3(0.004, 0.006, 0.012));
    cloudCol *= lerp(0.80, 1.0, density);
    float cloudAlpha = saturate(density * 1.15);

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

    return float4(col, saturate(alpha * Look.x));
}
