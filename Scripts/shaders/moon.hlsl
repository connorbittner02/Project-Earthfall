// M_EarthMoon. Fully procedural: dark maria, bright highlands and three scales of
// rimmed craters, bump-lit by the same sun vector as Earth. No atmosphere, so the
// terminator is hard. Tuning = (brightness, bump strength, earthshine, unused).
float MoonCraters(float3 p)
{
    float3 cell = floor(p);
    float3 f = p - cell;
    float3 h = Hash33(cell + 4.7);
    float3 centre = 0.30 + 0.40 * h;
    float radius = 0.10 + 0.22 * Hash13(cell + 8.3);
    float d = length(f - centre) / radius;
    float bowl = (smoothstep(0.0, 1.0, d) - 1.0) * step(d, 1.0);
    float rimWall = exp(-Pow2((d - 1.0) / 0.13)) * 0.40;
    return (bowl + rimWall) * step(0.45, h.z);
}

float MoonHeight(float3 n)
{
    float height = Fbm(n * 5.0 + 2.3, 4) * 0.55;
    height += MoonCraters(n * 3.4) * 0.60;
    height += MoonCraters(n * 8.0 + 13.0) * 0.26;
    height += MoonCraters(n * 19.0 + 41.0) * 0.11;
    return height;
}

float3 Main(float3 LocalPos, float3 Sun, float4 Tuning)
{
    float3 n = normalize(LocalPos);
    float3 t1 = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(1e-4, 0.0, 0.0));
    float3 t2 = cross(n, t1);
    float e = 0.004;
    float h0 = MoonHeight(n);
    float hx = MoonHeight(normalize(n + t1 * e));
    float hy = MoonHeight(normalize(n + t2 * e));
    float3 N = normalize(n - (t1 * (hx - h0) + t2 * (hy - h0)) * (Tuning.y / e));

    float maria = smoothstep(0.44, 0.58, Fbm(n * 1.5 + 7.1, 4));
    float albedo = lerp(0.050, 0.150, maria) * (0.80 + 0.45 * Fbm(n * 9.0, 3)) + saturate(h0 - 0.45) * 0.05;

    float NdL = saturate(dot(N, Sun)) * smoothstep(-0.02, 0.05, dot(n, Sun));
    float3 lit = float3(1.00, 0.98, 0.94) * albedo * NdL * Tuning.x;
    return lit + float3(0.30, 0.45, 0.80) * albedo * Tuning.z;
}
