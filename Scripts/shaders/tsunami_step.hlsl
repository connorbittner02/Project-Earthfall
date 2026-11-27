// M_TsunamiStep. One explicit step of the 2D wave equation on the equirectangular
// TsunamiRT ping-pong pair.
//   R = sea-surface height, G = its velocity, B = coastal inundation.
// Height and velocity are signed, but a material drawn to a render target has
// its negative output clamped to zero, so both are stored biased around 0.5:
//   R = height * 0.5 + 0.5, G = velocity * 0.05 + 0.5. Calm sea is (0.5, 0.5).
// Water carries the wave; land reflects it and records how hard it was hit, which
// the surface shader uses to draw foam and black out city lights along the coast.
// Step = (delta seconds, wave speed rad/s, texel width, texel height).
// Flood = (run-up kept per texel inland, shoreline gain): both rise with the
// volume of water the impact displaced, so a bigger strike floods further.
// The wave speed must match UEarthfallPopulationSubsystem::TsunamiSpeed, which
// times the CPU landfalls that trip power grids and cause casualties.
float3 Main(float2 UV, Texture2D WaveTex, SamplerState WaveTexSampler,
            Texture2D MasksTex, SamplerState MasksTexSampler, float4 Step, float4 Flood)
{
    float2 px = Step.zw;
    float2 offsets[4] = { float2(px.x, 0.0), float2(-px.x, 0.0), float2(0.0, px.y), float2(0.0, -px.y) };
    float3 here = Texture2DSampleLevel(WaveTex, WaveTexSampler, UV, 0).rgb;
    here.r = (here.r - 0.5) * 2.0;
    here.g = (here.g - 0.5) * 20.0;
    bool water = Texture2DSampleLevel(MasksTex, MasksTexSampler, UV, 0).r > 0.5;

    float neighbourHeight[4];
    float neighbourFlood = 0.0;
    float neighbourVelocity = 0.0;
    float strongestWave = 0.0;
    for (int i = 0; i < 4; i++)
    {
        float2 uv = UV + offsets[i];
        uv.y = clamp(uv.y, px.y, 1.0 - px.y);
        float3 tap = Texture2DSampleLevel(WaveTex, WaveTexSampler, uv, 0).rgb;
        tap.r = (tap.r - 0.5) * 2.0;
        tap.g = (tap.g - 0.5) * 20.0;
        bool neighbourWater = Texture2DSampleLevel(MasksTex, MasksTexSampler, uv, 0).r > 0.5;
        // A land neighbour mirrors this cell's height: a reflecting shoreline.
        neighbourHeight[i] = neighbourWater ? tap.r : here.r;
        neighbourVelocity += (neighbourWater ? tap.g : here.g) * 0.25;
        strongestWave = max(strongestWave, neighbourWater ? abs(tap.r) : 0.0);
        neighbourFlood = max(neighbourFlood, neighbourWater ? 0.0 : tap.b);
    }

    if (!water)
    {
        // Run-up: the shoreline takes the wave's height and the flood creeps a few
        // texels inland, losing a tenth of its strength each texel.
        float flood = max(here.b * exp(-Step.x / 45.0), max(strongestWave * Flood.y, neighbourFlood * Flood.x - 0.01));
        return float3(0.5, 0.5, saturate(flood));
    }

    float lat = (0.5 - UV.y) * 3.14159265;
    // Longitude texels shrink toward the poles; the clamp keeps the explicit step
    // inside its stability limit at the cost of slower waves at high latitude.
    float cosLat = max(cos(lat), 0.65);
    float spacing = px.x * 6.2831853;
    float laplacian = ((neighbourHeight[0] + neighbourHeight[1] - 2.0 * here.r) / (cosLat * cosLat)
                     + (neighbourHeight[2] + neighbourHeight[3] - 2.0 * here.r)) / (spacing * spacing);

    // A touch of velocity diffusion damps texel-scale ringing without slowing the front.
    float velocity = (lerp(here.g, neighbourVelocity, 0.08) + Step.y * Step.y * laplacian * Step.x) * exp(-Step.x * 0.06);
    float height = (here.r + velocity * Step.x) * exp(-Step.x * 0.02);
    // Sea ice and the polar pinch soak the wave up instead of ringing forever.
    float polar = smoothstep(1.25, 1.45, abs(lat));
    height *= 1.0 - polar;
    velocity *= 1.0 - polar;
    return float3(saturate(height * 0.5 + 0.5), saturate(velocity * 0.05 + 0.5), 0.0);
}
