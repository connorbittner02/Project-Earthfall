// M_Arcology. Megatowers and construction cranes raised around a city that has
// outgrown its baseline. Instanced boxes and cones; UV runs 0..1 across and down
// each face (an instance has no local position of its own to measure storeys by).
//   Look = (kind, storeys, lit level, 0). Kind 0 = glass tower, 1 = crane steel.
//   Mode 0 returns (base colour, roughness); Mode 1 returns (emissive, 0).
// Like the streamed buildings, they read the simulation maps by longitude and
// latitude: a tower inside a blast is a charred shell and one on a dead grid is dark.
float4 Main(float2 UV, float3 WorldPos, float3 NormalWS,
            Texture2D MaskTex, SamplerState MaskTexSampler,
            Texture2D GrowthTex, SamplerState GrowthTexSampler,
            float3 Sun, float Time, float4 Look, float Mode)
{
    float3 n = normalize(WorldPos);
    float2 uv = float2(atan2(-n.y, n.x) / 6.2831853 + 0.5,
                       0.5 - asin(clamp(n.z, -1.0, 1.0)) / 3.14159265);
    uv = WarpSimUV(uv, n);
    float3 Mask = Texture2DSampleLevel(MaskTex, MaskTexSampler, uv, 0).rgb;
    float3 Growth = Texture2DSampleLevel(GrowthTex, GrowthTexSampler, uv, 0).rgb;
    float ruin = saturate((Mask.r - 0.25) / 0.60 * 1.3);
    float power = (Growth.r < 0.002 ? 1.0 : saturate(Growth.b)) * (1.0 - ruin);

    float night = 1.0 - smoothstep(-0.20, 0.04, dot(n, Sun));
    float3 N = normalize(NormalWS);
    float roof = smoothstep(0.6, 0.9, abs(dot(N, n)));
    float up = 1.0 - UV.y;
    float across = UV.x;

    if (Look.x > 0.5)
    {
        // Crane: safety-yellow lattice. The braces are drawn, not modelled.
        float brace = step(0.5, frac(up * Look.y + across * 2.0)) * step(0.5, frac(up * Look.y - across * 2.0));
        float3 steel = lerp(float3(0.85, 0.52, 0.04), float3(0.30, 0.19, 0.02), brace);
        if (Mode > 0.5) return float4(float3(1.00, 0.55, 0.05) * night * power * (0.20 + 0.15 * brace), 0.0);
        return float4(lerp(steel, float3(0.03, 0.028, 0.026), ruin), 0.55);
    }

    // Tower: a dark glass curtain wall in storey bands, a lit sky-lobby every tenth
    // band and bright structural fins at the corners.
    float storey = up * Look.y;
    float band = smoothstep(0.10, 0.22, frac(storey)) * smoothstep(0.95, 0.83, frac(storey));
    float lobby = step(0.7, frac(floor(storey) / 4.0 + 0.05));
    float fin = smoothstep(0.06, 0.02, min(across, 1.0 - across));
    float bay = smoothstep(0.08, 0.16, frac(across * 6.0)) * smoothstep(0.92, 0.84, frac(across * 6.0));
    float lit = step(0.45, Hash13(float3(floor(storey), floor(across * 6.0), floor(N.x * 2.0 + N.y * 5.0))));

    if (Mode > 0.5)
    {
        float3 glow = float3(0.70, 0.88, 1.00) * band * bay * lit * 0.9;
        glow += float3(0.45, 0.95, 1.00) * lobby * band * 1.3;
        glow += float3(0.60, 0.92, 1.00) * fin * 1.6;
        // A slow pulse climbing the fins.
        glow *= 0.85 + 0.15 * sin(Time * 1.3 - up * 9.0);
        // By day the lobbies and fins still read; the office floors do not.
        float level = lerp(0.10 * (lobby + fin), 1.0, night) * Look.z * (1.0 - roof) * power;
        return float4(glow * level, 0.0);
    }
    float3 glass = lerp(float3(0.030, 0.050, 0.075), float3(0.090, 0.160, 0.215), band * bay);
    float3 frame = float3(0.30, 0.33, 0.36);
    float3 col = lerp(glass, frame, saturate(fin + (1.0 - band) * 0.22));
    col = lerp(col, float3(0.20, 0.22, 0.24), roof);
    col = lerp(col, float3(0.03, 0.028, 0.026), ruin);
    return float4(col, lerp(lerp(0.12, 0.45, saturate(fin + roof)), 0.95, ruin));
}
