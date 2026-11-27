// M_EarthAtmosphere. Additive unlit shell just above the cloud deck. The glow is
// derived from the view ray's closest approach to the planet centre, not from the
// mesh normal, so the limb stays a crisp analytic curve at any tessellation.
// Shape = (planet radius, shell radius, intensity, atmosphere health 0..1).
// The material is two-sided so the limb survives the camera dropping inside the
// shell in low orbit: from outside only the near faces draw, from inside the far.
float3 Main(float3 LocalPos, float3 CamLocal, float3 Sun, float4 Shape)
{
    float3 ray = normalize(LocalPos - CamLocal);
    // On a sphere a fragment faces the camera exactly when the ray is entering.
    bool cameraOutside = length(CamLocal) > Shape.y;
    bool farFace = dot(LocalPos, ray) > 0.0;
    if (cameraOutside && farFace) return float3(0.0, 0.0, 0.0);
    // A ray climbing away from the planet is closest to it at the camera.
    float along = max(dot(-CamLocal, ray), 0.0);
    float3 closest = CamLocal + ray * along;
    float impact = length(closest);
    float3 limbDir = closest / max(impact, 1e-3);

    float planetRadius = Shape.x;
    float altitude = (impact - planetRadius) / max(Shape.y - planetRadius, 1e-3);

    // Outside the limb the tangent path is twice as long, hence the bright line;
    // in front of the disc only a hairline is added because the surface shader
    // already carries the in-scatter there.
    float outside = exp(-max(altitude, 0.0) * 4.5) * Pow2(saturate(1.0 - altitude));
    float inside = pow(saturate(impact / planetRadius), 40.0) * 0.30;
    float glow = altitude >= 0.0 ? outside : inside;

    float sunAmount = dot(limbDir, Sun);
    float lit = smoothstep(-0.28, 0.25, sunAmount);
    float twilight = exp(-Pow2(sunAmount / 0.20));
    float3 rayleigh = lerp(float3(0.42, 0.30, 0.20), float3(0.16, 0.42, 1.00), Shape.w);
    float3 col = lerp(rayleigh, float3(1.00, 0.42, 0.12), twilight * 0.80);
    return col * glow * lit * Shape.z;
}
