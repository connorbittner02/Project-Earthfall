// M_AshAdvection and M_VapourAdvection. One semi-Lagrangian transport step for the
// two ping-pong maps that hold everything an impact lofts.
//   AshPlumeRT: R = airborne black carbon, G = radioactive fallout, B = ground deposit.
//   VapourRT:   R = steam, salt and ice mist, G = pulverised rock dust,
//               B = carbon x loft (so B / Ash.R is how high the carbon rides, 0..1).
// Step = (delta seconds, time, texel width, texel height). Mode 0 writes the ash
// map, Mode 1 the vapour map; both read the previous state of both.
//
// Five things decide what a plume does after the strike has stamped it:
//   1. what was hit decides what is in it (set by the stamp, in EarthfallPlanet);
//   2. the crater's kinetic energy decides how much rock dust (also the stamp);
//   3. wind shear: carbon lofted into the stratosphere outruns and outspreads the
//      low dust and steam, so one plume shears into a fast anvil and a slow skirt;
//   4. moisture: over open sea and in the wet latitude bands, rain scrubs carbon
//      and dust out of the lower air several times faster;
//   5. thermal buoyancy: the higher the carbon was lofted, the longer it stays.
//
// The wind field is WindVelocity() in common.hlsl.
float3 Main(float2 UV, Texture2D AshTex, SamplerState AshTexSampler,
            Texture2D FireTex, SamplerState FireTexSampler,
            Texture2D VapourTex, SamplerState VapourTexSampler,
            Texture2D MasksTex, SamplerState MasksTexSampler, float4 Step, float Mode)
{
    float lat = (0.5 - UV.y) * 3.14159265;
    float lon = (UV.x - 0.5) * 6.2831853;
    float2 wind = WindVelocity(lat, lon, Step.y);
    float cosLat = max(cos(lat), 0.08);
    float2 uvVelocity = float2(wind.x / (6.2831853 * cosLat), -wind.y / 3.14159265);
    float2 px = Step.zw;

    float3 hereAsh = Texture2DSampleLevel(AshTex, AshTexSampler, UV, 0).rgb;
    float3 hereVapour = Texture2DSampleLevel(VapourTex, VapourTexSampler, UV, 0).rgb;
    float loft = saturate(hereVapour.b / max(hereAsh.r, 0.02));

    // 3. Shear: the jet stream is several times the surface wind.
    float2 highSource = UV - uvVelocity * Step.x * (0.55 + 1.75 * loft);
    float2 lowSource = UV - uvVelocity * Step.x * 0.50;
    highSource.y = clamp(highSource.y, 0.002, 0.998);
    lowSource.y = clamp(lowSource.y, 0.002, 0.998);
    float spread = 0.10 + 0.22 * loft;

    // 4. Moisture: open water, the tropical rain belt and the mid-latitude storm tracks.
    float3 masks = Texture2DSampleLevel(MasksTex, MasksTexSampler, UV, 0).rgb;
    float water = smoothstep(0.55, 0.95, masks.r);
    float humidity = saturate(water * 0.65 + 0.55 * exp(-lat * lat / 0.030) + 0.35 * exp(-Pow2(abs(lat) - 0.90) / 0.030));
    // Rain falls from the weather; carbon above it is out of reach.
    float rain = humidity * (1.0 - 0.85 * loft);

    float3 ashCentre = Texture2DSampleLevel(AshTex, AshTexSampler, highSource, 0).rgb;
    float3 ashAround = (Texture2DSampleLevel(AshTex, AshTexSampler, highSource + float2(px.x, 0.0), 0).rgb
                      + Texture2DSampleLevel(AshTex, AshTexSampler, highSource - float2(px.x, 0.0), 0).rgb
                      + Texture2DSampleLevel(AshTex, AshTexSampler, highSource + float2(0.0, px.y), 0).rgb
                      + Texture2DSampleLevel(AshTex, AshTexSampler, highSource - float2(0.0, px.y), 0).rgb) * 0.25;
    // 5. Buoyancy: 170 s near the ground, three times that in the stratosphere.
    float sootKeep = exp(-Step.x * (1.0 + 2.5 * rain) / (170.0 * (1.0 + 2.0 * loft)));

    if (Mode < 0.5)
    {
        float2 airborne = lerp(ashCentre.rg, ashAround.rg, spread);
        // Wildfires feed their smoke straight into the plume, low down.
        float burning = Texture2DSampleLevel(FireTex, FireTexSampler, UV, 0).r;
        float soot = min(airborne.x * sootKeep + burning * Step.x * 0.22, 3.0);
        float fallout = airborne.y * exp(-Step.x * (1.0 + 1.5 * rain) / 110.0);
        float dustHere = hereVapour.g;
        float settled = hereAsh.b;
        // Rain brings it all down faster, so wet ground under a plume darkens first.
        float deposit = min(settled * exp(-Step.x / 900.0)
                          + (soot * 0.35 + fallout * 0.65 + dustHere * 0.45) * Step.x * 0.06 * (1.0 + 1.5 * rain), 1.0);
        return float3(soot, fallout, deposit);
    }

    float3 vapourLow = Texture2DSampleLevel(VapourTex, VapourTexSampler, lowSource, 0).rgb;
    float3 vapourLowAround = (Texture2DSampleLevel(VapourTex, VapourTexSampler, lowSource + float2(px.x, 0.0), 0).rgb
                            + Texture2DSampleLevel(VapourTex, VapourTexSampler, lowSource - float2(px.x, 0.0), 0).rgb
                            + Texture2DSampleLevel(VapourTex, VapourTexSampler, lowSource + float2(0.0, px.y), 0).rgb
                            + Texture2DSampleLevel(VapourTex, VapourTexSampler, lowSource - float2(0.0, px.y), 0).rgb) * 0.25;
    float2 lowAir = lerp(vapourLow.rg, vapourLowAround.rg, 0.10);
    // Steam recondenses and rains out in about a minute; over warm sea it lingers
    // as cloud a little longer. Rock dust is heavy and falls in about the same time.
    float steam = lowAir.x * exp(-Step.x / (55.0 + 40.0 * water));
    float dust = lowAir.y * exp(-Step.x * (1.0 + 2.0 * rain) / 75.0);
    // The lofted carbon moves with the carbon, and slowly sinks.
    float lofted = lerp(Texture2DSampleLevel(VapourTex, VapourTexSampler, highSource, 0).b,
                        (Texture2DSampleLevel(VapourTex, VapourTexSampler, highSource + float2(px.x, 0.0), 0).b
                       + Texture2DSampleLevel(VapourTex, VapourTexSampler, highSource - float2(px.x, 0.0), 0).b
                       + Texture2DSampleLevel(VapourTex, VapourTexSampler, highSource + float2(0.0, px.y), 0).b
                       + Texture2DSampleLevel(VapourTex, VapourTexSampler, highSource - float2(0.0, px.y), 0).b) * 0.25, spread);
    lofted = min(lofted * sootKeep * exp(-Step.x / 420.0), 3.0);
    return float3(min(steam, 3.0), min(dust, 3.0), lofted);
}
