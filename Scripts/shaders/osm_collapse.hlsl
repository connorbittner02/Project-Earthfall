// M_OSMBuildings world position offset: buildings inside a blast, a crater or a
// burn scar sink into the ground. The simulation maps are read per vertex, so a
// tower at the edge of the damage is sheared: one side stands, the other is gone.
//   WorldPos = vertex position before any offset. Params.x = metres per world unit.
// Each 90 m block of the city has its own threshold and depth, which leaves a
// ragged skyline of stumps and gaps rather than a clean cut-out.
float3 Main(float3 WorldPos,
            Texture2D MaskTex, SamplerState MaskTexSampler,
            Texture2D CraterTex, SamplerState CraterTexSampler,
            Texture2D FireTex, SamplerState FireTexSampler,
            float4 Params)
{
    float3 n = normalize(WorldPos);
    float2 uv = float2(atan2(-n.y, n.x) / 6.2831853 + 0.5,
                       0.5 - asin(clamp(n.z, -1.0, 1.0)) / 3.14159265);
    uv = WarpSimUV(uv, n);
    float mpu = Params.x;
    float3 Mask = Texture2DSampleLevel(MaskTex, MaskTexSampler, uv, 0).rgb;
    float2 crater = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv, 0).rg;
    float burnt = saturate(Texture2DSampleLevel(FireTex, FireTexSampler, uv, 0).g);

    float scorch = saturate((Mask.r - 0.25) / 0.60);
    float ruin = saturate(scorch * 1.3 + crater.r * 2.0 + crater.g);

    float3 cell = floor(WorldPos * mpu / 90.0);
    float threshold = Hash13(cell) * 0.55;
    float fall = smoothstep(threshold, threshold + 0.30, ruin + burnt * 0.30 * Hash13(cell + 7.0));
    // Most blocks go down completely; some keep a few storeys.
    float depth = fall * lerp(35.0, 460.0, sqrt(Hash13(cell + 3.0)));
    // What is left leans.
    float3 lean = (Hash33(cell) - 0.5) * 2.0;
    lean -= n * dot(lean, n);
    return (-n * depth + lean * fall * 22.0) / mpu;
}
