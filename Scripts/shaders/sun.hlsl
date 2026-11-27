// M_EarthSun (Mode.x = 0) and M_SunCorona (Mode.x = 1).
// Disc: HDR photosphere with limb darkening, far above bloom threshold.
// Corona: additive halo on a much larger sphere with diffraction spikes and an
// anamorphic streak, laid out in view space so the flare stays screen-aligned.
// Mode = (0 disc / 1 corona, intensity, unused, unused).
float3 Main(float3 NormalWS, float3 CamVec, float3 NormalView, float4 Mode)
{
    float mu = saturate(dot(normalize(NormalWS), normalize(CamVec)));
    float3 warm = float3(1.00, 0.84, 0.60);
    if (Mode.x < 0.5)
    {
        return warm * lerp(0.42, 1.0, sqrt(mu)) * Mode.y;
    }

    float r = sqrt(saturate(1.0 - mu * mu));
    float halo = exp(-r * 10.0) * 3.2 + exp(-r * 3.4) * 0.34;
    float angle = atan2(NormalView.y, NormalView.x);
    float spikes = pow(abs(cos(angle * 2.0)), 64.0) * exp(-r * 4.2) * 0.85
                 + pow(abs(cos(angle * 2.0 + 0.785398)), 96.0) * exp(-r * 7.5) * 0.45;
    float streak = exp(-abs(NormalView.y) * 30.0) * exp(-r * 2.4) * 0.45;
    float edgeFade = smoothstep(1.0, 0.72, r);
    return warm * (halo + spikes + streak) * edgeFade * Mode.y;
}
