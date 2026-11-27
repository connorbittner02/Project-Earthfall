// Opacity mask for M_EarthPhotoreal. In orbit Fade.x is 1 and every pixel passes.
// During the hand-over to streamed terrain it falls to 0 and the surface
// dissolves through a per-frame interleaved-gradient dither, which temporal
// anti-aliasing resolves into a smooth cross-fade onto the tiles beneath.
float Main(float2 Pixel, float Time, float4 Fade)
{
    float2 p = Pixel + frac(Time * 7.13) * float2(47.0, 17.0);
    float noise = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
    return Fade.x > noise * 0.998 ? 1.0 : 0.0;
}
