// Additive unlit effects materials, selected by Mode.x:
//   0 = M_FXGlow   soft view-facing glow on a sphere (fireballs, engine cores)
//   1 = M_FXTrail  ribbon with a hot centreline (exhaust and re-entry trails, beams)
//   2 = M_FXRing   shock band with a sharp leading edge and a soft wake
// Vertex colour carries per-vertex fade (8-bit), so the HDR level lives in Tint.a.
// Shape = (falloff exponent, hot-core boost, unused, unused).
float3 Main(float3 NormalWS, float3 CamVec, float2 UV, float3 VertexRGB, float VertexA, float4 Tint, float4 Shape, float4 Mode)
{
    float3 colour = Tint.rgb * Tint.a;
    if (Mode.x < 0.5)
    {
        float mu = abs(dot(normalize(NormalWS), normalize(CamVec)));
        return colour * (pow(mu, Shape.x) + pow(mu, Shape.x * 6.0) * Shape.y);
    }

    float3 faded = colour * VertexRGB * VertexA;
    if (Mode.x < 1.5)
    {
        float across = saturate(1.0 - abs(2.0 * UV.y - 1.0));
        return faded * (pow(across, Shape.x) + pow(across, Shape.x * 6.0) * Shape.y);
    }

    float v = saturate(UV.y);
    float band = pow(v, Shape.x) * smoothstep(1.0, 0.90, v);
    return faded * (band + pow(v, Shape.x * 5.0) * smoothstep(1.0, 0.94, v) * Shape.y);
}
