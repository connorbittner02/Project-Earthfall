// Parallax occlusion mapping for craters. Runs before every surface texture
// lookup in M_EarthPhotoreal and returns the UV the view ray actually lands on
// after marching through the crater height field in CraterRT:
//   R = bowl depth, G = raised ejecta rim and fracture rays.
// Craters gain real depth and rim overlap without moving a single vertex.
// Height zero is the reference plane, and the hit is interpolated between layers,
// so untouched terrain gets exactly zero offset and never swims with the view.
// Pom = (bowl depth in radians, rim height in radians, unused, unused).
float2 Main(float2 UV, float3 LocalPos, float3 ViewLocal,
            Texture2D CraterTex, SamplerState CraterTexSampler, float4 Pom)
{
    float3 n = normalize(LocalPos);
    float3 V = normalize(ViewLocal);
    float3 east = normalize(cross(n, float3(0.0, 0.0, 1.0)) + float3(0.0, 1e-5, 0.0));
    float3 north = cross(east, n);
    float cosLat = max(sqrt(saturate(1.0 - n.z * n.z)), 0.15);

    // UV shift per radian of height: U runs east, V runs south.
    float viewUp = max(dot(V, n), 0.25);
    float2 shiftPerHeight = float2(dot(V, east) / (6.2831853 * cosLat), -dot(V, north) / 3.14159265) / viewUp;

    const int Steps = 6;
    float top = Pom.y;
    float bottom = -Pom.x;
    float layer = (top - bottom) / Steps;

    // March down from the rim tops to the first layer under the surface, then
    // interpolate between the last two layers for the exact crossing.
    float hitHeight = bottom;
    float previousRay = top + layer;
    float previousSurface = 0.0;
    bool hit = false;
    for (int i = 0; i <= Steps; i++)
    {
        float rayHeight = top - layer * i;
        float2 crater = Texture2DSampleLevel(CraterTex, CraterTexSampler, UV + shiftPerHeight * rayHeight, 0).rg;
        float surface = crater.g * Pom.y - crater.r * Pom.x;
        if (!hit && surface >= rayHeight)
        {
            float above = max(previousRay - previousSurface, 0.0);
            float below = max(surface - rayHeight, 0.0);
            hitHeight = lerp(previousRay, rayHeight, above / max(above + below, 1e-6));
            hit = true;
        }
        previousRay = rayHeight;
        previousSurface = surface;
    }
    return UV + shiftPerHeight * hitHeight;
}
