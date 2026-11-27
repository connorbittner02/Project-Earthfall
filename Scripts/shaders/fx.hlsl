// Additive unlit effects materials, selected by Mode.x:
//   0 = M_FXGlow   soft view-facing glow on a sphere (fireballs, engine cores)
//   1 = M_FXTrail  ribbon with a hot centreline (exhaust and re-entry trails, beams)
//   2 = M_FXRing   shock band with a sharp leading edge and a soft wake
// Vertex colour carries per-vertex fade (8-bit), so the HDR level lives in Tint.a.
// Shape = (falloff exponent, hot-core boost, noise scale, turbulence 0..1).
float3 Main(float3 NormalWS, float3 CamVec, float2 UV, float3 VertexRGB, float VertexA, float3 LocalPos, float Time, float4 Tint, float4 Shape, float4 Mode)
{
    float3 colour = Tint.rgb * Tint.a;
    if (Mode.x < 0.5)
    {
        float mu = abs(dot(normalize(NormalWS), normalize(CamVec)));
        float edge = pow(mu, Shape.x);
        // Scrolling 3D noise eats into the view-dot fade, so the rim dissolves into
        // turbulent fire instead of ending on a clean silhouette.
        float3 q = normalize(LocalPos) * Shape.z + float3(0.0, 0.0, -Time * 0.9);
        float noise = saturate(Fbm(q, 4) * 1.6 - 0.25 + 0.35 * (VNoise(q * 3.7 + Time * 1.3) - 0.5));
        float body = edge * lerp(1.0, 0.30 + 1.35 * noise, Shape.w);
        body = max(body - Shape.w * 0.55 * (1.0 - noise) * (1.0 - edge), 0.0);
        return colour * (body + pow(mu, Shape.x * 6.0) * Shape.y);
    }

    float3 faded = colour * VertexRGB * VertexA;
    if (Mode.x < 1.5)
    {
        float across = saturate(1.0 - abs(2.0 * UV.y - 1.0));
        return faded * (pow(across, Shape.x) + pow(across, Shape.x * 6.0) * Shape.y);
    }

    float v = saturate(UV.y);
    // Pressure wave: a hard leading edge at v=1 and a long, thin trailing tail.
    float lead = smoothstep(1.0, 0.975, v);
    float tail = pow(v, Shape.x);
    return faded * lead * (tail * 0.45 + pow(v, Shape.x * 8.0) * Shape.y);
}
