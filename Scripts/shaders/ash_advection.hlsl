// M_AshAdvection. One semi-Lagrangian transport step for the ping-pong AshPlumeRT.
//   R = airborne soot, G = airborne radioactive fallout, B = ground deposition.
// R and G ride the wind; B stays where it lands and slowly weathers away.
// Step = (delta seconds, time, texel width, texel height).
//
// The wind field must stay in sync with FEarthfallWind::Velocity in C++, which
// advects the CPU-side plumes that drive fallout casualties and climate.
float2 WindVelocity(float lat, float lon, float time)
{
    // Easterly trades in the tropics, westerlies in the mid-latitudes.
    float zonal = -0.016 * cos(3.0 * lat) * (0.7 + 0.3 * cos(lat));
    zonal += 0.004 * sin(lat * 9.0 + lon * 2.0 + time * 0.021);
    float meridional = 0.006 * sin(lon * 3.0 + lat * 5.0 + time * 0.03) * cos(lat)
                     + 0.003 * sin(lon * 7.0 - time * 0.05);
    return float2(zonal, meridional);
}

float3 Main(float2 UV, Texture2D AshTex, SamplerState AshTexSampler, float4 Step)
{
    float lat = (0.5 - UV.y) * 3.14159265;
    float lon = (UV.x - 0.5) * 6.2831853;
    float2 wind = WindVelocity(lat, lon, Step.y);
    float cosLat = max(cos(lat), 0.08);
    float2 uvVelocity = float2(wind.x / (6.2831853 * cosLat), -wind.y / 3.14159265);

    float2 source = UV - uvVelocity * Step.x;
    source.y = clamp(source.y, 0.002, 0.998);
    float2 px = Step.zw;

    float3 centre = Texture2DSampleLevel(AshTex, AshTexSampler, source, 0).rgb;
    float3 around = (Texture2DSampleLevel(AshTex, AshTexSampler, source + float2(px.x, 0.0), 0).rgb
                   + Texture2DSampleLevel(AshTex, AshTexSampler, source - float2(px.x, 0.0), 0).rgb
                   + Texture2DSampleLevel(AshTex, AshTexSampler, source + float2(0.0, px.y), 0).rgb
                   + Texture2DSampleLevel(AshTex, AshTexSampler, source - float2(0.0, px.y), 0).rgb) * 0.25;
    float2 airborne = lerp(centre.rg, around.rg, 0.14);

    float soot = airborne.x * exp(-Step.x / 170.0);
    float fallout = airborne.y * exp(-Step.x / 110.0);

    float settled = Texture2DSampleLevel(AshTex, AshTexSampler, UV, 0).b;
    float deposit = min(settled * exp(-Step.x / 900.0) + (soot * 0.35 + fallout * 0.65) * Step.x * 0.06, 1.0);

    return float3(soot, fallout, deposit);
}
