// M_FireStep. One cellular-automaton step for the FireMaskRT ping-pong pair,
// run at a throttled ~16 Hz.
//   R = fire intensity, G = burn scar (persistent).
// A cell ignites when burning neighbours, weighted toward the upwind side, win a
// per-step dice roll scaled by its remaining fuel. Fuel is vegetated land: no
// ocean, no ice, and greener pixels of the day map burn more readily.
// Step = (delta seconds, time, texel width, texel height).
float3 Main(float2 UV, Texture2D FireTex, SamplerState FireTexSampler,
            Texture2D MasksTex, SamplerState MasksTexSampler,
            Texture2D DayTex, SamplerState DayTexSampler, float4 Step)
{
    float2 px = Step.zw;
    float3 here = Texture2DSampleLevel(FireTex, FireTexSampler, UV, 0).rgb;
    float3 masks = Texture2DSampleLevel(MasksTex, MasksTexSampler, UV, 0).rgb;
    float3 day = Texture2DSampleLevel(DayTex, DayTexSampler, UV, 0).rgb;

    float greenness = saturate((day.g - max(day.r, day.b) * 0.80) * 9.0);
    float vegetation = (1.0 - masks.r) * (1.0 - masks.b) * lerp(0.18, 1.0, greenness);
    float fuel = vegetation * saturate(1.0 - here.g * 1.15);

    float lat = (0.5 - UV.y) * 3.14159265;
    float lon = (UV.x - 0.5) * 6.2831853;
    float2 wind = WindVelocity(lat, lon, Step.y);
    float2 upwind = normalize(float2(wind.x, -wind.y) + float2(1e-5, 0.0)) * px * 1.5;

    // Fire arrives fastest from upwind, slower from every other side.
    float fromUpwind = Texture2DSampleLevel(FireTex, FireTexSampler, UV - upwind, 0).r;
    float around = max(max(Texture2DSampleLevel(FireTex, FireTexSampler, UV + float2(px.x, 0.0), 0).r,
                           Texture2DSampleLevel(FireTex, FireTexSampler, UV - float2(px.x, 0.0), 0).r),
                       max(Texture2DSampleLevel(FireTex, FireTexSampler, UV + float2(0.0, px.y), 0).r,
                           Texture2DSampleLevel(FireTex, FireTexSampler, UV - float2(0.0, px.y), 0).r));
    float pressure = saturate(max(fromUpwind, around * 0.45));

    float roll = Hash13(float3(UV * float2(1024.0, 512.0), frac(Step.y * 0.37) * 289.0));
    // Strictly greater: a zero chance must never win, even against a zero roll.
    float ignite = (pressure * fuel * Step.x * 2.6 > max(roll, 1e-4)) ? 1.0 : 0.0;

    float burnSeconds = 5.5;
    float fire = max(here.r * exp(-Step.x / burnSeconds), ignite) * step(0.02, vegetation);
    float scar = saturate(here.g + fire * Step.x / burnSeconds * 1.3);
    return float3(fire, scar, 0.0);
}
