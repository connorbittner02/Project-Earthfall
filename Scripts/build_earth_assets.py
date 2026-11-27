"""Import staged maps and author Earthfall materials in Unreal Editor 5.8.

Run with UnrealEditor-Cmd.exe Earthfall.uproject -ExecutePythonScript=...
Input PNGs are produced by prepare_earth_textures.py in Saved/SourceTextures.
"""
import os
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "Saved", "SourceTextures")
TEXTURE_DIR = "/Game/Earth/Textures"
MATERIAL_DIR = "/Game/Earth/Materials"


def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def import_texture(filename, asset_name):
    asset_path = TEXTURE_DIR + "/" + asset_name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(STAGE, filename)
    task.destination_path = TEXTURE_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(asset_path)
    if not texture:
        raise RuntimeError("Texture import failed: " + filename)
    texture.filter = unreal.TextureFilter.TF_TRILINEAR
    texture.address_x = unreal.TextureAddress.TA_WRAP
    texture.address_y = unreal.TextureAddress.TA_CLAMP
    texture.srgb = asset_name in ("T_Earth_Day", "T_Earth_Night")
    texture.compression_settings = unreal.TextureCompressionSettings.TC_DEFAULT
    if asset_name == "T_Earth_Normal":
        texture.compression_settings = unreal.TextureCompressionSettings.TC_NORMALMAP
        texture.srgb = False
    elif asset_name == "T_Earth_Height":
        texture.compression_settings = unreal.TextureCompressionSettings.TC_MASKS
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def create_material(name):
    path = MATERIAL_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Could not create material: " + name)
    return material


def node(material, node_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, node_class, x, y)


def connect(source, output, target, input_name):
    # UE's Python API names texture-sample pins (RGB/R/G/B), but single-output
    # material expressions expose an unnamed pin. Passing "Output" silently
    # leaves those connections open, so normalize every non-texture node here.
    source_class = source.get_class().get_name()
    source_output = output if "MaterialExpressionTextureSample" in source_class else ""
    target_class = target.get_class().get_name()
    # These expressions expose one unnamed input pin to MaterialEditingLibrary.
    # Passing the UObject property name ("Input") returns false and leaves the
    # pin disconnected even though the expression graph looks populated.
    target_input = "" if target_class in (
        "MaterialExpressionOneMinus",
        "MaterialExpressionComponentMask",
        "MaterialExpressionSaturate",
        "MaterialExpressionAbs",
    ) else input_name
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError("Could not connect %s -> %s.%s" % (source_class, target_class, target_input or "<first input>"))


def output(expression, expression_output, material_property):
    expression_class = expression.get_class().get_name()
    source_output = expression_output if "MaterialExpressionTextureSample" in expression_class else ""
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError("Could not connect %s to material property %s" %
                           (expression.get_class().get_name(), material_property))


def scalar(material, value, x, y):
    expr = node(material, unreal.MaterialExpressionConstant, x, y)
    expr.set_editor_property("r", value)
    return expr


def vector(material, value, x, y):
    expr = node(material, unreal.MaterialExpressionConstant3Vector, x, y)
    expr.set_editor_property("constant", value)
    return expr


def operation(material, cls, a, out_a, b, out_b, x, y):
    expr = node(material, cls, x, y)
    connect(a, out_a, expr, "A")
    connect(b, out_b, expr, "B")
    return expr


def multiply(material, a, out_a, b, out_b, x, y):
    return operation(material, unreal.MaterialExpressionMultiply, a, out_a, b, out_b, x, y)


def add(material, a, out_a, b, out_b, x, y):
    return operation(material, unreal.MaterialExpressionAdd, a, out_a, b, out_b, x, y)


def one_minus(material, source, source_output, x, y):
    expr = node(material, unreal.MaterialExpressionOneMinus, x, y)
    connect(source, source_output, expr, "Input")
    return expr


def component(material, source, source_output, channel, x, y):
    expr = node(material, unreal.MaterialExpressionComponentMask, x, y)
    expr.set_editor_property("r", channel == "R")
    expr.set_editor_property("g", channel == "G")
    expr.set_editor_property("b", channel == "B")
    expr.set_editor_property("a", channel == "A")
    connect(source, source_output, expr, "Input")
    return expr


def texture_parameter(material, name, texture, x, y, sampler=None):
    expr = node(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    expr.set_editor_property("parameter_name", name)
    if texture:
        expr.set_editor_property("texture", texture)
    if sampler is not None:
        expr.set_editor_property("sampler_type", sampler)
    return expr


def build_surface(day, night, clouds, height, normal):
    material = create_material("M_EarthPhotoreal")
    day_tex = texture_parameter(material, "DayTexture", day, -1000, -350,
                                unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    night_tex = texture_parameter(material, "NightTexture", night, -1000, -100,
                                  unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal_tex = texture_parameter(material, "NormalTexture", normal, -1000, 420,
                                   unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    impact_tex = texture_parameter(material, "ImpactMaskRT", clouds, -1000, 700,
                                    unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    # Land/water classification from the NASA blue-marble day map's blue dominance.
    blue = component(material, day_tex, "RGB", "B", -740, -560)
    red = component(material, day_tex, "RGB", "R", -740, -480)
    green = component(material, day_tex, "RGB", "G", -740, -400)
    red_weight = multiply(material, red, "R", scalar(material, 1.35, -740, -760), "Output", -500, -480)
    green_weight = multiply(material, green, "R", scalar(material, 0.92, -740, -690), "Output", -500, -400)
    non_water = add(material, red_weight, "Output", green_weight, "Output", -280, -420)
    water_delta = operation(material, unreal.MaterialExpressionSubtract, blue, "R", non_water, "Output", -80, -420)
    water_gain = multiply(material, water_delta, "Output", scalar(material, 3.2, -80, -620), "Output", 130, -420)
    water = node(material, unreal.MaterialExpressionSaturate, 330, -420)
    connect(water_gain, "Output", water, "Input")

    land_rough = scalar(material, 0.85, 320, -210)
    ocean_rough = scalar(material, 0.14, 320, -130)
    roughness = node(material, unreal.MaterialExpressionLinearInterpolate, 540, -180)
    connect(land_rough, "Output", roughness, "A")
    connect(ocean_rough, "Output", roughness, "B")
    connect(water, "Output", roughness, "Alpha")
    land_spec = scalar(material, 0.25, 320, -20)
    ocean_spec = scalar(material, 0.75, 320, 60)
    specular = node(material, unreal.MaterialExpressionLinearInterpolate, 540, 20)
    connect(land_spec, "Output", specular, "A")
    connect(ocean_spec, "Output", specular, "B")
    connect(water, "Output", specular, "Alpha")
    output(specular, "Output", unreal.MaterialProperty.MP_SPECULAR)

    mask_r = component(material, impact_tex, "RGB", "R", -740, 700)
    mask_g = component(material, impact_tex, "RGB", "G", -740, 790)
    mask_b = component(material, impact_tex, "RGB", "B", -740, 850)
    vertex_normal = node(material, unreal.MaterialExpressionVertexNormalWS, -740, 930)
    sun_direction = node(material, unreal.MaterialExpressionVectorParameter, -740, 1070)
    sun_direction.set_editor_property("parameter_name", "SunDirection")
    sun_direction.set_editor_property("default_value", unreal.LinearColor(0.65, 0.24, 0.72, 0.0))
    ndotl = operation(material, unreal.MaterialExpressionDotProduct, vertex_normal, "Output",
                      sun_direction, "Output", -500, 930)
    biased = add(material, ndotl, "Output", scalar(material, 0.12, -500, 1100), "Output", -280, 930)
    scaled = multiply(material, biased, "Output", scalar(material, 1.0 / 0.30, -280, 1100), "Output", -80, 930)
    day_linear = node(material, unreal.MaterialExpressionSaturate, 130, 930)
    connect(scaled, "Output", day_linear, "Input")
    day_sq = multiply(material, day_linear, "Output", day_linear, "Output", 330, 930)
    two_day = multiply(material, day_linear, "Output", scalar(material, 2.0, 130, 1120), "Output", 330, 1090)
    three_minus = operation(material, unreal.MaterialExpressionSubtract, scalar(material, 3.0, 330, 1200),
                            "Output", two_day, "Output", 540, 1090)
    day_curve = multiply(material, day_sq, "Output", three_minus, "Output", 750, 930)
    night_weight = one_minus(material, day_curve, "Output", 960, 930)
    blackout = one_minus(material, mask_r, "R", 960, 730)
    light_mask = multiply(material, night_weight, "Output", blackout, "Output", 1160, 850)
    night_color = multiply(material, night_tex, "RGB", light_mask, "Output", 1370, 820)
    night_brightness = multiply(material, night_color, "Output", scalar(material, 55.0, 1370, 1010), "Output", 1570, 820)
    heat_color = vector(material, unreal.LinearColor(1.0, 0.10, 0.015, 1.0), 960, 1170)
    hot = multiply(material, heat_color, "Output", mask_g, "R", 1160, 1110)
    hot_brightness = multiply(material, hot, "Output", scalar(material, 24.0, 1370, 1190), "Output", 1570, 1100)
    day_complement = one_minus(material, day_linear, "Output", 960, 1510)
    terminator_band = multiply(material, day_linear, "Output", day_complement, "Output", 1160, 1510)
    scattering_color = vector(material, unreal.LinearColor(0.16, 0.045, 0.008, 1.0), 1370, 1510)
    scattering = multiply(material, terminator_band, "Output", scattering_color, "Output", 1570, 1450)
    # A small emission floor preserves readable NASA albedo under the required
    # low-lux orbital sun, while the lit base-color path still supplies PBR shading.
    # High dynamic-range daylight albedo maintains visibility with the deliberately
    # restrained 3.2-lux orbital key. The term fades smoothly to zero on the night side.
    day_readability_raw = multiply(material, day_tex, "RGB", scalar(material, 100.0, 1780, 1190), "Output", 1990, 1160)
    day_readability = multiply(material, day_readability_raw, "Output", day_curve, "Output", 2190, 1160)
    fresnel = node(material, unreal.MaterialExpressionFresnel, 960, 1370)
    fresnel.set_editor_property("exponent", 4.2)
    rim_color = vector(material, unreal.LinearColor(0.10, 0.38, 1.0, 1.0), 1160, 1370)
    rim = multiply(material, fresnel, "Output", rim_color, "Output", 1370, 1370)
    emissive_a = add(material, night_brightness, "Output", hot_brightness, "Output", 1780, 890)
    emissive_b = add(material, emissive_a, "Output", rim, "Output", 1990, 940)
    emissive_c = add(material, emissive_b, "Output", scattering, "Output", 2200, 940)
    emissive = add(material, emissive_c, "Output", day_readability, "Output", 2410, 940)
    output(emissive, "Output", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Near-camera micro relief: a tiled normal detail layer fades in below orbital altitude.
    detail_uv = node(material, unreal.MaterialExpressionTextureCoordinate, -500, 520)
    detail_uv.set_editor_property("u_tiling", 48.0)
    detail_uv.set_editor_property("v_tiling", 48.0)
    detail_normal = texture_parameter(material, "DetailNormal", normal, -280, 520,
                                      unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    connect(detail_uv, "Output", detail_normal, "UVs")
    ocean_detail_uv = node(material, unreal.MaterialExpressionTextureCoordinate, -500, 600)
    ocean_detail_uv.set_editor_property("u_tiling", 112.0)
    ocean_detail_uv.set_editor_property("v_tiling", 112.0)
    ocean_detail = texture_parameter(material, "OceanDetailNormal", normal, -280, 640,
                                     unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    connect(ocean_detail_uv, "Output", ocean_detail, "UVs")
    camera_position = node(material, unreal.MaterialExpressionCameraPositionWS, -500, 700)
    world_position = node(material, unreal.MaterialExpressionWorldPosition, -500, 820)
    camera_distance = operation(material, unreal.MaterialExpressionDistance, camera_position, "Output",
                                world_position, "Output", -280, 750)
    close_detail = node(material, unreal.MaterialExpressionSmoothStep, -60, 750)
    close_detail.set_editor_property("const_min", 340.0)
    close_detail.set_editor_property("const_max", 130.0)
    connect(camera_distance, "Output", close_detail, "Value")
    biome_detail = node(material, unreal.MaterialExpressionLinearInterpolate, 0, 540)
    connect(detail_normal, "RGB", biome_detail, "A")
    connect(ocean_detail, "RGB", biome_detail, "B")
    connect(water, "Output", biome_detail, "Alpha")
    normal_detail_blend = node(material, unreal.MaterialExpressionLinearInterpolate, 180, 540)
    connect(normal_tex, "RGB", normal_detail_blend, "A")
    connect(biome_detail, "Output", normal_detail_blend, "B")
    connect(close_detail, "Output", normal_detail_blend, "Alpha")
    output(normal_detail_blend, "Output", unreal.MaterialProperty.MP_NORMAL)

    micro_signal = component(material, biome_detail, "Output", "R", 400, 30)
    micro_variation = multiply(material, micro_signal, "R", scalar(material, 0.035, 400, 140), "Output", 600, 30)
    micro_roughness = add(material, roughness, "Output", micro_variation, "Output", 800, -30)
    distance_roughness = node(material, unreal.MaterialExpressionLinearInterpolate, 1000, -50)
    connect(roughness, "Output", distance_roughness, "A")
    connect(micro_roughness, "Output", distance_roughness, "B")
    connect(close_detail, "Output", distance_roughness, "Alpha")
    output(distance_roughness, "Output", unreal.MaterialProperty.MP_ROUGHNESS)

    uv = node(material, unreal.MaterialExpressionTextureCoordinate, -740, 1770)
    uv_v = component(material, uv, "Output", "G", -500, 1770)
    polar_centered = operation(material, unreal.MaterialExpressionSubtract, uv_v, "R",
                               scalar(material, 0.5, -500, 1950), "Output", -280, 1770)
    polar_abs = node(material, unreal.MaterialExpressionAbs, -80, 1770)
    connect(polar_centered, "Output", polar_abs, "Input")
    polar_delta = operation(material, unreal.MaterialExpressionSubtract, polar_abs, "Output",
                            scalar(material, 0.38, -80, 1950), "Output", 130, 1770)
    polar_scaled = multiply(material, polar_delta, "Output", scalar(material, 8.0, 130, 1950), "Output", 330, 1770)
    polar_mask = node(material, unreal.MaterialExpressionSaturate, 540, 1770)
    connect(polar_scaled, "Output", polar_mask, "Input")
    polar_darkening = multiply(material, polar_mask, "Output", scalar(material, 0.22, 540, 1950), "Output", 750, 1770)
    polar_albedo = one_minus(material, polar_darkening, "Output", 960, 1770)
    # Preserve the real satellite albedo; the relief proxy is reserved for normals.
    polar_day = multiply(material, day_tex, "RGB", polar_albedo, "Output", 1160, 1770)
    scorch_factor = multiply(material, mask_r, "R", scalar(material, 0.82, 1160, 1950), "Output", 1370, 1770)
    unscorched = one_minus(material, scorch_factor, "Output", 1570, 1770)
    darkened_day = multiply(material, polar_day, "Output", unscorched, "Output", 1780, 1770)

    # Soft cloud drop-shadows shift the sampled cloud cover slightly along the sun vector.
    cloud_tex = texture_parameter(material, "CloudShadowTexture", clouds, -740, 2130,
                                  unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    shadow_uv = node(material, unreal.MaterialExpressionTextureCoordinate, -740, 2320)
    sun_u = component(material, sun_direction, "Output", "R", -500, 2130)
    sun_v = component(material, sun_direction, "Output", "G", -500, 2220)
    offset_u = multiply(material, sun_u, "R", scalar(material, 0.0015, -500, 2400), "Output", -280, 2130)
    offset_v = multiply(material, sun_v, "R", scalar(material, 0.0015, -500, 2490), "Output", -280, 2220)
    uv_offset = node(material, unreal.MaterialExpressionAppendVector, -60, 2190)
    connect(offset_u, "Output", uv_offset, "A")
    connect(offset_v, "Output", uv_offset, "B")
    shadow_coordinates = add(material, shadow_uv, "Output", uv_offset, "Output", 180, 2220)
    connect(shadow_coordinates, "Output", cloud_tex, "UVs")
    cloud_coverage = component(material, cloud_tex, "RGB", "R", 400, 2220)
    cloud_not_cleared = one_minus(material, mask_b, "B", 600, 2220)
    shadow_coverage = multiply(material, cloud_coverage, "R", cloud_not_cleared, "Output", 800, 2220)
    shadow_amount = multiply(material, shadow_coverage, "Output", scalar(material, 0.18, 800, 2400), "Output", 1000, 2220)
    sunlit_albedo = multiply(material, darkened_day, "Output", one_minus(material, shadow_amount, "Output", 1200, 2220), "Output", 1410, 2190)
    output(sunlit_albedo, "Output", unreal.MaterialProperty.MP_BASE_COLOR)

    material.set_editor_property("two_sided", False)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    for property_name in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS,
                          unreal.MaterialProperty.MP_SPECULAR, unreal.MaterialProperty.MP_NORMAL,
                          unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        if not unreal.MaterialEditingLibrary.get_material_property_input_node(material, property_name):
            raise RuntimeError("Missing material input: " + str(property_name))
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_DIR + "/M_EarthPhotoreal")
    return material


def build_clouds(clouds, height):
    material = create_material("M_EarthClouds")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", True)
    cloud = texture_parameter(material, "CloudTexture", clouds, -700, -100,
                              unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    impact = texture_parameter(material, "ImpactMaskRT", clouds, -700, 150,
                               unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    clear = component(material, impact, "RGB", "B", -450, 180)
    remaining = one_minus(material, clear, "R", -220, 170)
    coverage = multiply(material, cloud, "R", remaining, "Output", 10, -30)
    opacity = multiply(material, coverage, "Output", scalar(material, 0.10, 10, 160), "Output", 240, -30)
    output(opacity, "Output", unreal.MaterialProperty.MP_OPACITY)
    output(vector(material, unreal.LinearColor(0.78, 0.86, 1.0, 1.0), 15, -270), "Output",
           unreal.MaterialProperty.MP_BASE_COLOR)
    glow = multiply(material, cloud, "RGB", vector(material, unreal.LinearColor(0.05, 0.07, 0.10, 1.0), 250, -250),
                    "Output", 480, -120)
    output(glow, "Output", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_DIR + "/M_EarthClouds")
    return material


def build_atmosphere():
    material = create_material("M_EarthAtmosphere")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    fresnel = node(material, unreal.MaterialExpressionFresnel, -450, 0)
    fresnel.set_editor_property("exponent", 4.2)
    tint = vector(material, unreal.LinearColor(0.11, 0.42, 1.0, 1.0), -200, 0)
    color = multiply(material, fresnel, "Output", tint, "Output", 20, 0)
    opacity = multiply(material, fresnel, "Output", scalar(material, 0.22, -200, 150), "Output", 20, 200)
    output(color, "Output", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    output(opacity, "Output", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_DIR + "/M_EarthAtmosphere")


def build_space(stars):
    material = create_material("M_SpaceSky")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    star_tex = texture_parameter(material, "StarfieldTexture", stars, -350, 0)
    glow = multiply(material, star_tex, "RGB", scalar(material, 16.0, -100, 180), "Output", -20, 80)
    output(glow, "Output", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_DIR + "/M_SpaceSky")


def build_sun():
    material = create_material("M_EarthSun")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    solar_color = vector(material, unreal.LinearColor(1.0, 0.72, 0.38, 1.0), 0, 0)
    solar_emission = multiply(material, solar_color, "Output", scalar(material, 120.0, 0, 180), "Output", 220, 0)
    output(solar_emission, "Output", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_DIR + "/M_EarthSun")


def main():
    ensure_dir(TEXTURE_DIR)
    ensure_dir(MATERIAL_DIR)
    day = import_texture("T_Earth_Day.png", "T_Earth_Day")
    night = import_texture("T_Earth_Night.png", "T_Earth_Night")
    clouds = import_texture("T_Earth_Clouds.png", "T_Earth_Clouds")
    height = import_texture("T_Earth_Height.png", "T_Earth_Height")
    normal = import_texture("T_Earth_Normal.png", "T_Earth_Normal")
    stars = import_texture("T_Earth_Starfield.png", "T_Earth_Starfield")
    # Serialize every source texture before compiling materials that reference them.
    for texture in (day, night, clouds, height, normal, stars):
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
    build_surface(day, night, clouds, height, normal)
    build_clouds(clouds, height)
    build_atmosphere()
    build_space(stars)
    build_sun()
    unreal.EditorAssetLibrary.save_directory("/Game/Earth", only_if_is_dirty=False, recursive=True)
    unreal.log("EARTHFALL_ASSET_BUILD_COMPLETE")


main()
