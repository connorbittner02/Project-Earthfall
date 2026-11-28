// M_FXFireball. A detonation's incandescent gas, drawn on a sphere as a translucent
// volume; returns (colour, opacity).
//   Heat = (core temperature in kelvin, brightness, soot 0..1, seed).
//   Shape = (noise scale, turbulence 0..1, churn speed, edge exponent).
// Every pixel has a temperature: hottest where the view ray passes through the
// middle of the ball, cooler toward the rim and in the troughs of a multi-octave,
// domain-warped turbulence that rolls upward. That temperature is turned into
// colour and radiance by the black-body curve, so one ball shows white-yellow
// cores, orange convection cells and deep red seams at once, and the mix slides
// down the curve as the core cools. Gas that has fallen below about 1300 K stops
// glowing and becomes what it is by then: opaque black carbon. That soot jacket
// closes over the fire from the rim inward.
float4 Main(float3 NormalWS, float3 CamVec, float3 LocalPos, float Time, float4 Heat, float4 Shape)
{
    float mu = abs(dot(normalize(NormalWS), normalize(CamVec)));
    float3 dir = normalize(LocalPos);

    // Three octaves bend the lookup for five more: rolling cells, not static noise.
    float3 q = dir * Shape.x + float3(Heat.w, Heat.w * 1.7, -Time * Shape.z);
    float3 roll = float3(Fbm(q, 3), Fbm(q + 19.3, 3), Fbm(q + 47.1, 3)) - 0.44;
    float cells = Fbm(q * 1.9 + roll * 2.4 + float3(0.0, 0.0, -Time * Shape.z * 0.6), 5);
    float fine = VNoise(q * 7.5 + roll * 5.0 - float3(0.0, 0.0, Time * Shape.z * 1.4));
    float turbulence = saturate(cells * 1.7 - 0.28 + (fine - 0.5) * 0.30);

    // How much gas the ray passes through: full in the middle, nothing at the
    // silhouette, with the turbulence biting lobes out of the edge.
    float depth = pow(mu, Shape.w);
    float body = saturate(depth * (0.55 + 0.85 * turbulence) - (1.0 - turbulence) * (1.0 - mu) * 0.75 * Shape.y);
    body = smoothstep(0.03, 0.55, body);

    float kelvin = Heat.x * (0.42 + 0.58 * pow(mu, 0.8)) * lerp(1.0, 0.45 + 0.95 * turbulence, Shape.y);
    float3 glow = BlackbodyColour(kelvin) * BlackbodyPower(kelvin) * Heat.y;

    // Carbon condenses out of whatever has cooled.
    float carbon = Heat.z * smoothstep(1700.0, 1000.0, kelvin);
    float3 sootCol = float3(0.020, 0.016, 0.013) * (0.5 + turbulence);
    float3 col = lerp(glow, sootCol + glow * 0.12, carbon);
    // Glowing gas is thin; soot is opaque.
    float opacity = body * saturate(lerp(saturate(BlackbodyPower(kelvin) * 2.2 + 0.25), 1.0, carbon));
    return float4(col, saturate(opacity));
}
