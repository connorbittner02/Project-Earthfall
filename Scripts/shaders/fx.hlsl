// Additive unlit effects materials, selected by Mode.x:
//   0 = M_FXGlow   soft view-facing glow on a sphere (fireballs, engine cores)
//   1 = M_FXTrail  ribbon with a hot centreline (exhaust and re-entry trails, beams)
//   2 = M_FXRing   shock band with a sharp leading edge and a soft wake
//   3 = M_FXSpray  a curtain of thrown water: UV.x runs round it, UV.y up it
//   4 = M_FXWilson a Wilson condensation dome: a thin shell of fog behind a shock
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

    if (Mode.x > 3.5)
    {
        // The low pressure behind a blast wave condenses the air into a thin, crisp
        // shell. Seen from outside a shell is brightest where the view ray runs
        // along it, at the limb, and nearly clear face on. Shape.x sharpens the
        // limb; Shape.y is how much the shell is torn, which grows as it thins.
        float muW = abs(dot(normalize(NormalWS), normalize(CamVec)));
        float limb = pow(1.0 - muW, Shape.x);
        float3 qW = normalize(LocalPos) * Shape.z;
        float tear = Fbm(qW + float3(0.0, 0.0, Time * 0.4), 4);
        float streaks = VNoise(qW * float3(1.0, 1.0, 6.0) + 3.0);
        float shell = (limb * 1.25 + 0.05) * saturate(1.0 - Shape.y * (1.25 - tear * 1.9)) * (0.80 + 0.35 * streaks);
        // Flat on the ground the dome would cut a hard line: it fades into it.
        shell *= smoothstep(-0.20, 0.25, normalize(LocalPos).z);
        return colour * shell;
    }

    float3 faded = colour * VertexRGB * VertexA;
    if (Mode.x > 2.5)
    {
        // Vaporised sea spray. Brightness is opacity for an additive sheet: it
        // builds from the waterline, thins to nothing at the top and outer rim,
        // and rising streaky noise eats holes through it. The noise is looked up
        // on a circle so the curtain has no seam.
        float v = saturate(UV.y);
        float angle = UV.x * 6.2831853;
        // Shape.z = frequency along the sheet, Shape.w = frequency round it (0 = defaults).
        float alongFreq = Shape.z > 0.0 ? Shape.z : 3.2;
        float roundFreq = Shape.w > 0.0 ? Shape.w : 7.0;
        float3 q = float3(cos(angle) * roundFreq, sin(angle) * roundFreq, v * alongFreq - Time * 1.1);
        float streaks = Fbm(q, 4);
        float mist = VNoise(q * float3(2.6, 2.6, 0.9) + float3(0.0, 0.0, -Time * 0.7));
        float taper = smoothstep(0.0, 0.22, v) * pow(1.0 - v, Shape.x);
        float body = saturate(taper * (0.30 + 1.25 * streaks) - (1.0 - mist) * 0.42 * (0.35 + v));
        // Grazing sheets stack up along the view ray; fade them so the rim stays soft.
        float facing = abs(dot(normalize(NormalWS), normalize(CamVec)));
        return faded * body * (0.45 + 0.55 * facing) * (1.0 + Shape.y * streaks);
    }
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
