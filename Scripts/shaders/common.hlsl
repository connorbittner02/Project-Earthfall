// Shared Earthfall shader library. build_earth_assets.py pastes this file, then one
// material file, inside `struct FEarthfall { ... }` in a Custom material node, so
// every function here is a struct method and must be defined before it is used.
// Pure HLSL only: no engine globals, so validate_shaders.py can compile it with fxc.

float Pow2(float x) { return x * x; }

float Hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float3 Hash33(float3 p)
{
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// Trilinear value noise, 0..1.
float VNoise(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(Hash13(i), Hash13(i + float3(1, 0, 0)), f.x),
                   lerp(Hash13(i + float3(0, 1, 0)), Hash13(i + float3(1, 1, 0)), f.x), f.y);
    float b = lerp(lerp(Hash13(i + float3(0, 0, 1)), Hash13(i + float3(1, 0, 1)), f.x),
                   lerp(Hash13(i + float3(0, 1, 1)), Hash13(i + float3(1, 1, 1)), f.x), f.y);
    return lerp(a, b, f.z);
}

float Fbm(float3 p, int octaves)
{
    float amplitude = 0.5;
    float sum = 0.0;
    for (int i = 0; i < octaves; i++)
    {
        sum += amplitude * VNoise(p);
        p = p * 2.03 + float3(17.1, 3.7, 9.2);
        amplitude *= 0.5;
    }
    return sum;
}

float3 RotateAxis(float3 v, float3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// Unit direction -> equirectangular UV. U=0.5 is +X (0E), V=0 is +Z (north pole).
float2 DirToUV(float3 n)
{
    return float2(atan2(-n.y, n.x) / 6.2831853 + 0.5, acos(clamp(n.z, -1.0, 1.0)) / 3.14159265);
}

// ---------------------------------------------------------------------------
// Shockwaves. Each slot: A.xyz = centre (unit, planet-local), A.w = front angular
// radius in radians. B = (push strength, flash intensity, flash radius, ring glow).
// C.rgb = flash colour.
// ---------------------------------------------------------------------------

// Returns the direction to sample so cloud appears shoved outward by the front,
// and accumulates the front intensity so the caller can add condensation.
float3 ShockDisplace(float3 n, float4 A, float4 B, inout float ring)
{
    float cosd = dot(n, A.xyz);
    float d = acos(clamp(cosd, -1.0, 1.0));
    float width = 0.030 + A.w * 0.20;
    float front = exp(-Pow2((d - A.w) / width)) * B.x;
    float3 away = n - A.xyz * cosd;
    float len = length(away);
    away = len > 1e-4 ? away / len : float3(0.0, 0.0, 0.0);
    ring += front;
    return normalize(n - away * front * width * 1.8);
}

float3 ShockFlash(float3 n, float4 A, float4 B, float4 C)
{
    float3 toLight = A.xyz * 1.03 - n;
    float dist2 = dot(toLight, toLight);
    float radius = max(B.z, 1e-3);
    // The planet occludes its own far side: light only reaches a cap around ground zero.
    float reach = min(radius * 2.5 + 0.25, 3.0);
    float horizon = saturate((dot(n, A.xyz) - cos(reach)) / (1.0 - cos(reach)));
    return C.rgb * (B.y * horizon / (1.0 + dist2 / (radius * radius)));
}

// ---------------------------------------------------------------------------
// Procedural weather. n is a unit planet-local direction; the result is cloud
// density 0..1 with exactly 0 in clear sky. Layer = (seed, threshold, zonal
// stretch, drift rad/s). Shared by the cloud shells and the surface cloud shadow
// so shadows line up with the clouds that cast them.
// ---------------------------------------------------------------------------
float CloudField(float3 n, float time, float4 layer, int octaves)
{
    // Differential zonal drift: the tropics outrun the mid-latitudes.
    float drift = time * layer.w * (0.6 + 0.4 * cos(n.z * 2.6));
    float cs = cos(drift);
    float sn = sin(drift);
    n = float3(n.x * cs - n.y * sn, n.x * sn + n.y * cs, n.z);

    // Six cyclones, alternating hemispheres. Twisting the lookup around each
    // centre winds the fbm into spiral arms.
    for (int i = 0; i < 6; i++)
    {
        float3 h = Hash33(float3(i * 7.31 + layer.x, 3.7, 9.1));
        float hemisphere = (i % 2 == 0) ? 1.0 : -1.0;
        float sinLat = (0.32 + 0.46 * h.x) * hemisphere;
        float lon = h.y * 6.2831853;
        float cosLat = sqrt(1.0 - sinLat * sinLat);
        float3 centre = float3(cosLat * cos(lon), cosLat * sin(lon), sinLat);
        float chord2 = max(2.0 - 2.0 * dot(n, centre), 0.0);
        float radius = 0.15 + 0.13 * h.z;
        float twist = hemisphere * 1.9 * exp(-chord2 / (radius * radius));
        n = RotateAxis(n, centre, twist);
    }

    float3 q = n * 1.8 + layer.x;
    float3 warp = float3(VNoise(q + float3(0.0, time * 0.004, 0.0)), VNoise(q + 31.7), VNoise(q + 71.3)) - 0.5;
    float3 p = float3(n.xy * 4.3, n.z * 4.3 * layer.z) + warp * 1.15 + layer.x * 1.7;
    float f = Fbm(p, octaves);
    // Fine erosion breaks solid decks into cells and streets.
    f += (Fbm(p * 5.3 + 3.1, 3) - 0.44) * 0.11;

    // Climate bands: wet ITCZ, dry subtropical highs, stormy mid-latitudes.
    float sl = abs(n.z);
    float climate = 0.070 * exp(-sl * sl / 0.012)
                  - 0.075 * exp(-Pow2(sl - 0.42) / 0.020)
                  + 0.060 * exp(-Pow2(sl - 0.78) / 0.030);
    return smoothstep(layer.y, layer.y + 0.15, f + climate);
}
