// M_EarthClouds. Unlit translucent shell; returns (colour, opacity). Clear sky is
// exactly 0 opacity, so the layer can never film the oceans. Layer = (seed,
// threshold, zonal stretch, drift). Look = (opacity scale, self-shadow strength,
// ash visibility, sun intensity). Quality = (march steps, deck thickness in
// radians, unused, unused).
//
// The deck is a volume, not a painted shell. The view ray is marched down through
// a layer of real thickness in Quality.x steps; a cloud stands as tall as it is
// dense, so the march sees the sides of towers, and parallax moves them against
// the ground as the camera moves. Each sample is lit by Beer-Lambert attenuation
// along a second lookup toward the sun and through the cloud above it, which is
// what makes sunward edges bright, lee sides grey and the undersides of storms
// dark. A two-lobe Henyey-Greenstein phase function adds the silver lining where
// the view ray runs close to the sun through thin cloud.
float4 Main(float3 LocalPos, float3 ViewLocal, float CamDist, float2 UV, float3 Mask,
            Texture2D AshTex, SamplerState AshTexSampler, Texture2D VapourTex, SamplerState VapourTexSampler,
            float3 Sun, float Time, float4 Layer, float4 Look, float4 Quality,
            float4 A0, float4 B0, float4 C0, float4 A1, float4 B1, float4 C1,
            float4 A2, float4 B2, float4 C2, float4 A3, float4 B3, float4 C3)
{
    float3 n = normalize(LocalPos);
    float3 V = normalize(ViewLocal);
    float3 Ash = Texture2DSampleLevel(AshTex, AshTexSampler, UV, 0).rgb;
    float3 Vapour = Texture2DSampleLevel(VapourTex, VapourTexSampler, UV, 0).rgb;

    // Shockwaves shove the weather outward and leave a condensation front.
    float ring = 0.0;
    float3 ns = ShockDisplace(n, A0, B0, ring);
    ns = ShockDisplace(ns, A1, B1, ring);
    ns = ShockDisplace(ns, A2, B2, ring);
    ns = ShockDisplace(ns, A3, B3, ring);
    ring = saturate(ring);
    float cleared = saturate(Mask.b);

    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);

    // The weather domain here, and how it changes a step east and a step north.
    float sinLat;
    float sinLatUnused;
    float3 p0 = CloudDomain(ns, Time, Layer, sinLat);
    float3 pE = (CloudDomain(normalize(ns + east * 0.012), Time, Layer, sinLatUnused) - p0) / 0.012;
    float3 pN = (CloudDomain(normalize(ns + north * 0.012), Time, Layer, sinLatUnused) - p0) / 0.012;

    // High-frequency wispy erosion: thin cloud is eaten away first, so decks
    // fray into filaments instead of ending on a solid white edge. Value noise
    // shows its lattice as straight creases up close, so the detail lookups are
    // bent through a slow vector warp first. One erosion value serves every step.
    float3 warp = float3(VNoise(ns * 17.0), VNoise(ns * 17.0 + 31.0), VNoise(ns * 17.0 + 57.0)) - 0.5;
    float3 wp = ns * 46.0 + warp * 1.6 + float3(Time * 0.012, 0.0, Time * 0.007);
    float wisps = Fbm(wp, 3) * 0.7 + Fbm(wp * 3.1 + 5.0, 2) * 0.3;
    float erode = smoothstep(0.30, 0.62, wisps) * 0.42;
    float nearDetail = 1.0 - smoothstep(4.0, 40.0, CamDist);
    [branch] if (nearDetail > 0.001)
    {
        float fine = Fbm(ns * 420.0 + warp * 9.0 + (wisps - 0.5) * 3.0 + float3(Time * 0.02, 0.0, 0.0), 3);
        erode += smoothstep(0.35, 0.75, fine) * 0.45 * nearDetail;
    }
    float fineStructure = (Fbm(p0 * 5.3 + 3.1, 3) - 0.44) * 0.11;

    float NdL = dot(n, Sun);
    float3 sunTangent = Sun - n * NdL;
    float2 sunFlat = float2(dot(sunTangent, east), dot(sunTangent, north));
    float day = smoothstep(-0.12, 0.20, NdL);
    float3 sunCol = lerp(float3(1.0, 0.40, 0.14), float3(1.0, 0.98, 0.95), smoothstep(-0.02, 0.38, NdL)) * Look.w;
    float3 skyCol = float3(0.10, 0.16, 0.28) * 0.55 * day + float3(0.004, 0.006, 0.012);

    // The march. The ray enters at this pixel and slides sideways as it descends:
    // steeply when seen face on, a long way when the deck is seen at a grazing angle.
    float vUp = dot(V, n);
    float3 slide = -(V - n * vUp) / max(abs(vUp), 0.22);
    float2 slideFlat = float2(dot(slide, east), dot(slide, north));
    float thickness = Quality.y;
    // The thin high deck (Look.y < 1) cannot shadow itself visibly: two steps do.
    int steps = Look.y >= 1.0 ? (int)Quality.x : 2;
    float stepDepth = 1.0 / steps;
    // Towards the sun: the forward lobe is the silver lining, the back lobe keeps
    // the day side from going flat.
    float cosSun = dot(-V, Sun);
    float phase = lerp(PhaseHG(cosSun, -0.25), min(PhaseHG(cosSun, 0.72), 9.0), 0.55);

    // Six or eight slices through a deck would show as terraces. Starting each
    // pixel's march at a different depth turns the terraces into fine grain,
    // which temporal anti-aliasing then averages away.
    float dither = Hash13(n * 9173.0 + frac(Time * 0.37) * 11.0);
    float transmittance = 1.0;
    float3 scattered = float3(0.0, 0.0, 0.0);
    float cover = 0.0;
    for (int k = 0; k < 8; k++)
    {
        if (k >= steps) break;
        // Depth 0 is the top of the layer, 1 its base; a camera under the deck marches up.
        float depth = (k + dither) * stepDepth;
        float height = vUp >= 0.0 ? 1.0 - depth : depth;
        float2 at = slideFlat * thickness * depth;
        float3 p = p0 + pE * at.x + pN * at.y;
        float field = saturate(CloudDensity(p, sinLat, Layer, 3) + fineStructure);
        // Thin cloud is eaten away first; thick cores keep most of themselves.
        field = saturate(field - erode * (1.0 - field * 0.55));
        field = saturate(field + ring * 0.28) * (1.0 - cleared);
        cover = max(cover, field);
        // A cloud stands as tall as it is dense: thin veils hug the base of the
        // layer, storm cores fill it.
        float top = 0.34 + 0.66 * sqrt(field);
        float local = field * saturate((top - height) * 2.4);
        [branch] if (local > 0.002)
        {
            // Light reaching this sample: through the cloud between it and the sun
            // (one lookup a third of a layer up-sun) and through its own column above.
            float sunUp = max(NdL, 0.10);
            float2 toSun = at + sunFlat * thickness * 0.34 / sunUp;
            float lit = saturate(CloudDensity(p0 + pE * toSun.x + pN * toSun.y, sinLat, Layer, 2) + fineStructure - erode * 0.5) * (1.0 - cleared);
            float litTop = 0.34 + 0.66 * sqrt(lit);
            float blocker = lit * saturate((litTop - (height + 0.34)) * 2.4);
            float above = field * saturate(top - height);
            float beam = exp(-(blocker * 1.9 + above * 1.0) * Look.y);
            // Light scattered many times inside a cloud never falls to nothing.
            beam = 0.22 + 0.78 * beam;
            // Sky light comes from above too: bases are darker than tops.
            float ambient = lerp(0.42, 1.0, height) * exp(-above * 1.3);
            float3 radiance = 0.86 * (sunCol * day * beam * (0.42 + 0.50 * phase * (0.35 + 0.65 * beam)) + skyCol * ambient);
            float opacity = 1.0 - exp(-local * 7.0 * stepDepth / max(abs(vUp), 0.30));
            scattered += transmittance * opacity * radiance;
            transmittance *= 1.0 - opacity;
        }
    }
    // Semi-transparent vapour: even the thickest cores stop at 0.86 so ocean and
    // land read through.
    float cloudAlpha = (1.0 - transmittance) * 0.86;
    float3 cloudCol = scattered / max(1.0 - transmittance, 1e-4);

    // Towering ash plumes shadow the cloud deck: three samples toward the sun.
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

    // What an impact lofts rides above the weather, and is not one grey smoke.
    // Black carbon (Ash.r) absorbs; pulverised rock (Vapour.g) is a tan haze; steam
    // and salt mist from the sea, or ice mist (Vapour.r), are white and lit like
    // cloud. Fallout (Ash.g) tints the carbon. A 0.7 power lifts thin plumes so
    // they read from orbit.
    float soot = pow(saturate(Ash.r), 0.7);
    float fallout = saturate(Ash.g);
    float steam = pow(saturate(Vapour.r), 0.7);
    float dust = pow(saturate(Vapour.g), 0.7);
    float sootAlpha = saturate(soot * 1.6) * 0.92 * Look.z;
    float dustAlpha = saturate(dust * 1.2) * 0.50 * Look.z;
    float steamAlpha = saturate(steam * 1.2) * 0.72 * Look.z;
    float3 sootCol = float3(0.075, 0.058, 0.046) * (0.10 + 0.90 * day) * Look.w
                   + float3(0.30, 0.55, 0.06) * fallout * 0.07;
    float3 dustCol = float3(0.46, 0.36, 0.25) * (0.08 + 0.92 * day) * Look.w;
    float3 steamCol = (sunCol * day * (0.55 + 0.25 * phase) + skyCol) * 0.92;
    // Carbon over dust over steam: the darkest, highest layer hides the others.
    float veilAlpha = 1.0 - (1.0 - sootAlpha) * (1.0 - dustAlpha) * (1.0 - steamAlpha);
    float3 veilCol = (sootCol * sootAlpha
                    + dustCol * dustAlpha * (1.0 - sootAlpha)
                    + steamCol * steamAlpha * (1.0 - sootAlpha) * (1.0 - dustAlpha)) / max(veilAlpha, 1e-4);

    float alpha = veilAlpha + cloudAlpha * (1.0 - veilAlpha);
    float3 col = (veilCol * veilAlpha + cloudCol * cloudAlpha * (1.0 - veilAlpha)) / max(alpha, 1e-4);

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
    alpha *= smoothstep(0.03, 0.18, abs(vUp));
    return float4(col, saturate(alpha * Look.x));
}
