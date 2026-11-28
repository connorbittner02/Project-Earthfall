// M_OSMBuildings. Shades the Cesium OSM Buildings tileset: real building footprints
// and heights for every mapped city, which arrive untextured.
//   WorldPos = pixel position in world space (the planet sits at the origin, 100
//   units in radius). Params.x = metres per world unit.
//   Mode 0 returns (base colour, roughness); Mode 1 returns (emissive, 0).
// Facades are Poly Haven concrete, glazed in bands and bays; roofs are Poly Haven
// aerial asphalt. The same equirectangular simulation maps the terrain decal reads
// are sampled here by longitude and latitude, so a district inside a blast is
// charred, one in a wildfire burns, and a city whose grid has failed is dark.
float4 Main(float3 WorldPos, float3 NormalWS, float CamDist,
            Texture2D ConcreteA, SamplerState ConcreteASampler,
            Texture2D ConcreteNR, SamplerState ConcreteNRSampler,
            Texture2D AsphaltA, SamplerState AsphaltASampler,
            Texture2D MaskTex, SamplerState MaskTexSampler,
            Texture2D CraterTex, SamplerState CraterTexSampler,
            Texture2D FireTex, SamplerState FireTexSampler,
            Texture2D WaveTex, SamplerState WaveTexSampler,
            Texture2D GrowthTex, SamplerState GrowthTexSampler,
            float3 Sun, float Time, float4 Params, float Mode)
{
    float3 n = normalize(WorldPos);
    float2 uv = float2(atan2(-n.y, n.x) / 6.2831853 + 0.5,
                       0.5 - asin(clamp(n.z, -1.0, 1.0)) / 3.14159265);
    uv = WarpSimUV(uv, n);
    float mpu = Params.x;

    float3 Mask = Texture2DSampleLevel(MaskTex, MaskTexSampler, uv, 0).rgb;
    float2 crater = Texture2DSampleLevel(CraterTex, CraterTexSampler, uv, 0).rg;
    float2 Fire = Texture2DSampleLevel(FireTex, FireTexSampler, uv, 0).rg;
    float3 Wave = Texture2DSampleLevel(WaveTex, WaveTexSampler, uv, 0).rgb;
    float3 Growth = Texture2DSampleLevel(GrowthTex, GrowthTexSampler, uv, 0).rgb;

    float scorch = saturate((Mask.r - 0.25) / 0.60);
    float ruin = saturate(scorch * 1.3 + crater.r * 2.0 + crater.g);
    float burning = smoothstep(0.05, 0.6, Fire.r);
    float burnt = saturate(Fire.g);
    float flood = saturate(Wave.b * 3.0);
    // The growth map's red is never zero once bound; blue is the cell's grid health.
    float power = Growth.r < 0.002 ? 1.0 : saturate(Growth.b);
    float night = 1.0 - smoothstep(-0.20, 0.04, dot(n, Sun));

    // Wall frame: up the building, and along the wall.
    float3 N = normalize(NormalWS);
    float roof = smoothstep(0.55, 0.85, abs(dot(N, n)));
    float3 along = normalize(cross(n, N) + float3(1e-5, 0.0, 0.0));
    float hM = dot(WorldPos, n) * mpu;
    float tM = dot(WorldPos, along) * mpu;
    // Metres one pixel covers: decides which scale of glazing can be drawn.
    float footprint = CamDist * mpu * 0.00053;

    // A 90 m block of the city shares a look, so neighbours differ.
    float3 cell = floor(WorldPos * mpu / 90.0);
    float style = Hash13(cell);
    float glassy = step(0.55, style);

    // Fine glazing: 3 m bays, 3.6 m storeys. Resolved only from close in.
    float2 g = float2(tM / 3.0, hM / 3.6);
    float2 gf = frac(g);
    float2 gi = floor(g);
    float pane = smoothstep(0.12, 0.20, gf.x) * smoothstep(0.88, 0.80, gf.x)
               * smoothstep(0.25, 0.33, gf.y) * smoothstep(0.85, 0.77, gf.y);
    float fineFade = saturate(1.6 - footprint / 1.5);
    // Coarse glazing: curtain-wall panels four storeys by four bays, which is what
    // is left of a facade from 30,000 ft.
    float2 gc = float2(tM / 12.0, hM / 14.4);
    float2 cf = frac(gc);
    float3 ci = float3(floor(gc), style * 91.0);
    float panel = smoothstep(0.04, 0.12, cf.x) * smoothstep(0.96, 0.88, cf.x)
                * smoothstep(0.06, 0.16, cf.y) * smoothstep(0.94, 0.84, cf.y);
    float coarseFade = saturate(2.2 - footprint / 4.5);
    float panelRandom = Hash13(ci + cell.zxy * 0.13);

    if (Mode > 0.5)
    {
        // Lit windows. Each pane is on or off; from far away that averages to a
        // panel whose brightness is how many of its rooms are lit.
        float litPane = step(0.62, Hash13(float3(gi, 9.7) + cell)) * pane;
        float litPanel = smoothstep(0.30, 0.95, panelRandom) * lerp(0.55, panel, coarseFade);
        float lit = lerp(litPanel * 0.45, litPane, fineFade) * (1.0 - roof);
        // Sodium-warm offices with the odd cool-white LED floor.
        float3 lightCol = lerp(float3(1.00, 0.72, 0.42), float3(0.78, 0.90, 1.00), step(0.82, Hash13(ci + 5.5)));
        // A failing grid browns out floor by floor before it goes dark.
        float flicker = power > 0.85 ? 1.0 : step(Hash13(ci + floor(Time * 7.0)), power * 1.1);
        float alive = power * flicker * (1.0 - ruin) * (1.0 - flood) * (1.0 - burnt);
        float3 emissive = lightCol * lit * night * alive * 9.0 * (0.6 + 0.4 * glassy);
        // Fire inside: storeys glow and gutter through their window openings.
        float flame = burning * (1.0 - roof) * (0.35 + 0.65 * VNoise(WorldPos * mpu / 25.0 + float3(0.0, 0.0, Time * 3.0)))
                    * step(0.30, Hash13(ci + floor(Time * 2.0)));
        emissive += float3(1.00, 0.30, 0.04) * flame * lerp(0.6, 1.0, panel) * 6.0;
        // Roofs of burning blocks glow through.
        emissive += float3(1.00, 0.20, 0.02) * burning * roof * VNoise(WorldPos * mpu / 14.0 + Time) * 2.5;
        return float4(emissive, 0.0);
    }

    // Day shading. Concrete and asphalt tile every 250 m: far larger than life, so
    // the scans read as weathering and patchwork roofs rather than averaging to grey.
    float lod = DetailLod(CamDist, 0.25, 2048.0);
    float3 tw = TriWeights(N);
    float3 tp = WorldPos * mpu / 250.0;
    float3 concrete = TriPlanar(ConcreteA, ConcreteASampler, tp, tw, lod).rgb;
    // The scan is a warm, stained wall. Most of a skyline is greyer than that.
    concrete = lerp(concrete, dot(concrete, float3(0.30, 0.59, 0.11)).xxx, 0.75) * 1.5;
    float concreteRough = TriPlanar(ConcreteNR, ConcreteASampler, tp, tw, lod).b;
    float3 asphalt = TriPlanar(AsphaltA, ConcreteASampler, tp * 1.9 + 0.31, tw, lod).rgb;

    // Cladding: mostly pale concrete and stone, some brick and sandstone.
    float cladding = Hash13(cell + 11.0);
    float3 tint = lerp(float3(0.92, 0.90, 0.86), float3(1.18, 1.14, 1.08), cladding);
    tint = lerp(tint, float3(0.86, 0.60, 0.48), step(0.80, cladding));
    float3 wall = concrete * tint * 1.25;
    float3 glass = lerp(float3(0.060, 0.095, 0.135), float3(0.150, 0.215, 0.275), panelRandom);
    float glazing = lerp(lerp(0.42, panel * (0.55 + 0.45 * panelRandom), coarseFade), pane, fineFade) * lerp(0.60, 1.0, glassy);
    float3 facade = lerp(wall, glass, glazing);
    float3 roofCol = asphalt * lerp(0.75, 1.9, Hash13(cell + 5.0));
    float3 col = lerp(facade, roofCol, roof);
    float rough = lerp(lerp(concreteRough, 0.22, glazing), 0.85, roof);

    // Burnt out, blasted or drowned.
    float charred = saturate(ruin * 1.2 + burnt * 0.85);
    col = lerp(col, float3(0.030, 0.027, 0.025) * (0.6 + 0.8 * concrete.r), charred);
    col = lerp(col, col * float3(0.42, 0.40, 0.36), flood * 0.7);
    rough = lerp(rough, 0.95, charred);
    // At night the only light on a facade is the city's own: none on a dead grid.
    col *= lerp(1.0, 0.08 + 0.92 * power * (1.0 - ruin), night);
    return float4(col, rough);
}
