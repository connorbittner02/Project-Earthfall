"""Import staged NASA maps and author every Earthfall material in Unreal Editor 5.8.

    UnrealEditor-Cmd.exe Earthfall.uproject -run=pythonscript -script=Scripts/build_earth_assets.py

Textures come from Saved/SourceTextures (see prepare_earth_textures.py). Set the
environment variable EARTHFALL_SKIP_TEXTURES=1 to keep the already-imported
texture assets and rebuild only materials, which is the fast shader-iteration path.

Each material is a handful of input nodes feeding ONE Custom node whose body is
Scripts/shaders/common.hlsl plus one material file. Keeping the shading in real
HLSL files makes it reviewable in a diff and lets validate_shaders.py compile it
with fxc before the editor is ever launched.
"""
import os
import re
import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "Saved", "SourceTextures")
SHADERS = os.path.join(ROOT, "Scripts", "shaders")
TEXTURE_DIR = "/Game/Earth/Textures"
MATERIAL_DIR = "/Game/Earth/Materials"
FX_MATERIAL_DIR = "/Game/Earthfall/Materials"
FX_DIR = "/Game/Earthfall/FX"
BLACK = "/Engine/EngineResources/Black"

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
SHOCK_SLOTS = 4

OBSOLETE_ASSETS = (
    MATERIAL_DIR + "/M_EarthPhotoreal_RoughOcean",
    MATERIAL_DIR + "/M_SpaceSky_WISE",
    FX_MATERIAL_DIR + "/M_EarthfallSurface",
    FX_MATERIAL_DIR + "/M_EarthfallShockwave",
    TEXTURE_DIR + "/T_Earth_Clouds",
    TEXTURE_DIR + "/T_Earth_Height",
    TEXTURE_DIR + "/T_Earth_Starfield",
)


# ---------------------------------------------------------------------------
# Assets
# ---------------------------------------------------------------------------
def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def import_texture(filename, asset_name, srgb, compression):
    asset_path = TEXTURE_DIR + "/" + asset_name
    source = os.path.join(STAGE, filename)
    if os.environ.get("EARTHFALL_SKIP_TEXTURES") == "1" and EAL.does_asset_exist(asset_path):
        return unreal.load_asset(asset_path)
    if not os.path.isfile(source):
        raise RuntimeError("Missing staged texture (run prepare_earth_textures.py): " + source)
    if EAL.does_asset_exist(asset_path):
        EAL.delete_asset(asset_path)
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = TEXTURE_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(asset_path)
    if not texture:
        raise RuntimeError("Texture import failed: " + filename)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_TRILINEAR)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    # The globe is a procedural mesh with no streaming data, so keep full mips resident.
    texture.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(texture)
    unreal.log("EARTHFALL imported " + asset_name)
    return texture


def create_material(directory, name):
    path = directory + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, directory, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Could not create material: " + path)
    return material


def finish_material(material, directory, name):
    errors = MEL.recompile_material(material)
    if errors:
        raise RuntimeError("Material %s failed to translate:\n%s" % (name, "\n".join(str(e) for e in errors)))
    if not EAL.save_asset(directory + "/" + name):
        raise RuntimeError("Could not save material: " + name)
    unreal.log("EARTHFALL built " + name)


# ---------------------------------------------------------------------------
# Graph helpers
# ---------------------------------------------------------------------------
def node(material, node_class, x=0, y=0):
    expression = MEL.create_material_expression(material, node_class, x, y)
    if not expression:
        raise RuntimeError("Could not create " + str(node_class))
    return expression


def connect(source, output_name, target, input_name):
    if not MEL.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError("Could not connect %s.%s -> %s.%s" % (
            source.get_class().get_name(), output_name or "<first>",
            target.get_class().get_name(), input_name or "<first>"))


def output(source, output_name, material_property):
    if not MEL.connect_material_property(source, output_name, material_property):
        raise RuntimeError("Could not connect %s to %s" % (source.get_class().get_name(), material_property))


def vector_parameter(material, name, default, y=0):
    expression = node(material, unreal.MaterialExpressionVectorParameter, -900, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", unreal.LinearColor(*default))
    return expression


def constant4(material, value, y=0):
    expression = node(material, unreal.MaterialExpressionConstant4Vector, -900, y)
    expression.set_editor_property("constant", unreal.LinearColor(*value))
    return expression


def texture_sample(material, name, texture, sampler, y=0):
    expression = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("texture", texture)
    expression.set_editor_property("sampler_type", sampler)
    return expression


def texture_object(material, name, texture, y=0):
    expression = node(material, unreal.MaterialExpressionTextureObjectParameter, -900, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("texture", texture)
    return expression


def local_position(material, y=0):
    """Pixel position in the primitive's local space (plain float3, never LWC)."""
    world = node(material, unreal.MaterialExpressionWorldPosition, -1300, y)
    transform = node(material, unreal.MaterialExpressionTransformPosition, -1100, y)
    transform.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    transform.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", transform, "")
    return transform


def camera_local(material, y=0):
    camera = node(material, unreal.MaterialExpressionCameraPositionWS, -1300, y)
    transform = node(material, unreal.MaterialExpressionTransformPosition, -1100, y)
    transform.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    transform.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(camera, "", transform, "")
    return transform


def transform_vector(material, source, destination, y=0):
    transform = node(material, unreal.MaterialExpressionTransform, -1100, y)
    transform.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    transform.set_editor_property("transform_type", destination)
    connect(source, "", transform, "")
    return transform


def local_vector(material, source_class, y=0):
    return transform_vector(material, node(material, source_class, -1300, y),
                            unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL, y)


def read_shader(name):
    with open(os.path.join(SHADERS, name), "r", encoding="utf-8") as handle:
        return handle.read()


def custom(material, shader_file, inputs, output_type):
    """Create one Custom node running common.hlsl + shader_file.

    inputs is an ordered list of (name, source_expression, source_output_name) that
    must match the HLSL Main() signature. A texture object expands to the texture
    and its sampler, exactly as the material translator names them.
    """
    body = read_shader(shader_file)
    signature = re.search(r"\bMain\s*\(([^)]*)\)", body, re.S)
    declared = [part.split()[-1] for part in signature.group(1).split(",")]
    arguments = []
    for name, source, _ in inputs:
        arguments.append(name)
        if isinstance(source, unreal.MaterialExpressionTextureObjectParameter):
            arguments.append(name + "Sampler")
    if arguments != declared:
        raise RuntimeError("%s: graph inputs %s do not match Main(%s)" % (shader_file, arguments, declared))

    expression = node(material, unreal.MaterialExpressionCustom, -300, 0)
    expression.set_editor_property("description", os.path.splitext(shader_file)[0])
    expression.set_editor_property("output_type", output_type)
    custom_inputs = []
    for name, _, _ in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    expression.set_editor_property("inputs", custom_inputs)
    expression.set_editor_property("code", "struct FEarthfall\n{\n%s\n%s\n};\nFEarthfall Earthfall;\nreturn Earthfall.Main(%s);\n" % (
        read_shader("common.hlsl"), body, ", ".join(arguments)))
    for name, source, source_output in inputs:
        connect(source, source_output, expression, name)
    return expression


def shock_inputs(material, y):
    """Vector parameters for the shockwave slots, in Main() order A, B, C per slot."""
    inputs = []
    for slot in range(SHOCK_SLOTS):
        for letter in "ABC":
            parameter = vector_parameter(material, "Shock%s%d" % (letter, slot), (0.0, 0.0, 0.0, 0.0), y)
            inputs.append(("%s%d" % (letter, slot), parameter, "RGBA"))
            y += 60
    return inputs


def mask(material, source, channels):
    expression = node(material, unreal.MaterialExpressionComponentMask, 0, 200)
    for channel in "rgba":
        expression.set_editor_property(channel, channel in channels)
    connect(source, "", expression, "")
    return expression


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------
def build_surface(day, night, normal, masks, black):
    material = create_material(MATERIAL_DIR, "M_EarthPhotoreal")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    camera_distance = node(material, unreal.MaterialExpressionDistance, -1100, -200)
    connect(node(material, unreal.MaterialExpressionCameraPositionWS, -1300, -240), "", camera_distance, "A")
    connect(node(material, unreal.MaterialExpressionWorldPosition, -1300, -160), "", camera_distance, "B")
    inputs = [
        ("LocalPos", local_position(material, -600), ""),
        ("NormalLocal", local_vector(material, unreal.MaterialExpressionVertexNormalWS, -500), ""),
        ("ViewLocal", local_vector(material, unreal.MaterialExpressionCameraVectorWS, -400), ""),
        ("CamDist", camera_distance, ""),
        ("DayTex", texture_sample(material, "DayTexture", day, color, -100), "RGB"),
        ("NightTex", texture_sample(material, "NightTexture", night, color, 100), "RGB"),
        ("NormalTex", texture_sample(material, "NormalTexture", normal, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 300), "RGB"),
        ("MasksTex", texture_sample(material, "MasksTexture", masks, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 500), "RGB"),
        ("Mask", texture_sample(material, "ImpactMaskRT", black, color, 700), "RGB"),
        ("Ash", texture_sample(material, "AshPlumeRT", black, color, 900), "RGB"),
        ("Sun", vector_parameter(material, "SunDirection", (1.0, 0.0, 0.0, 0.0), 1100), "RGB"),
        ("Time", node(material, unreal.MaterialExpressionTime, -900, 1200), ""),
        ("CloudLayer", vector_parameter(material, "CloudLayer", (0.0, 0.52, 1.25, 0.004), 1300), "RGBA"),
        ("Tuning", vector_parameter(material, "Tuning", (1.75, 3.2, 1.0, 2.6), 1400), "RGBA"),
    ] + shock_inputs(material, 1500)
    shading = custom(material, "earth_surface.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, "M_EarthPhotoreal")


def build_clouds(black):
    material = create_material(MATERIAL_DIR, "M_EarthClouds")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    inputs = [
        ("LocalPos", local_position(material, -600), ""),
        ("Mask", texture_sample(material, "ImpactMaskRT", black, color, -400), "RGB"),
        ("Ash", texture_sample(material, "AshPlumeRT", black, color, -200), "RGB"),
        ("Sun", vector_parameter(material, "SunDirection", (1.0, 0.0, 0.0, 0.0), 0), "RGB"),
        ("Time", node(material, unreal.MaterialExpressionTime, -900, 100), ""),
        ("Layer", vector_parameter(material, "Layer", (0.0, 0.52, 1.25, 0.004), 200), "RGBA"),
        ("Look", vector_parameter(material, "Look", (1.0, 1.7, 1.0, 1.55), 300), "RGBA"),
    ] + shock_inputs(material, 400)
    shading = custom(material, "earth_clouds.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    output(mask(material, shading, "rgb"), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    output(mask(material, shading, "a"), "", unreal.MaterialProperty.MP_OPACITY)
    finish_material(material, MATERIAL_DIR, "M_EarthClouds")


def build_atmosphere():
    material = create_material(MATERIAL_DIR, "M_EarthAtmosphere")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    inputs = [
        ("LocalPos", local_position(material, -200), ""),
        ("CamLocal", camera_local(material, -100), ""),
        ("Sun", vector_parameter(material, "SunDirection", (1.0, 0.0, 0.0, 0.0), 0), "RGB"),
        ("Shape", vector_parameter(material, "Shape", (100.0, 104.5, 1.0, 1.0), 100), "RGBA"),
    ]
    shading = custom(material, "earth_atmosphere.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, "M_EarthAtmosphere")


def build_space(sky):
    material = create_material(MATERIAL_DIR, "M_SpaceSky")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    inputs = [
        ("CamVec", node(material, unreal.MaterialExpressionCameraVectorWS, -900, -100), ""),
        ("SkyTex", texture_object(material, "SpaceSkybox", sky, 0), ""),
        ("Tuning", vector_parameter(material, "Tuning", (1.1, 1.0, 0.03, 0.0), 100), "RGBA"),
    ]
    shading = custom(material, "space_sky.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, "M_SpaceSky")


def build_sun(name, mode, additive):
    material = create_material(MATERIAL_DIR, name)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if additive:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    normal = node(material, unreal.MaterialExpressionVertexNormalWS, -1300, 0)
    inputs = [
        ("NormalWS", normal, ""),
        ("CamVec", node(material, unreal.MaterialExpressionCameraVectorWS, -900, -100), ""),
        ("NormalView", transform_vector(material, normal, unreal.MaterialVectorCoordTransform.TRANSFORM_VIEW, 100), ""),
        ("Mode", vector_parameter(material, "Mode", mode, 200), "RGBA"),
    ]
    shading = custom(material, "sun.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, name)


def build_moon():
    material = create_material(MATERIAL_DIR, "M_EarthMoon")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    inputs = [
        ("LocalPos", local_position(material, -100), ""),
        ("Sun", vector_parameter(material, "SunDirection", (1.0, 0.0, 0.0, 0.0), 0), "RGB"),
        ("Tuning", vector_parameter(material, "Tuning", (3.2, 0.030, 0.010, 0.0), 100), "RGBA"),
    ]
    shading = custom(material, "moon.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, "M_EarthMoon")


def build_ash_advection(black):
    material = create_material(MATERIAL_DIR, "M_AshAdvection")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    inputs = [
        ("UV", node(material, unreal.MaterialExpressionTextureCoordinate, -900, -100), ""),
        ("AshTex", texture_object(material, "AshSource", black, 0), ""),
        ("Step", vector_parameter(material, "Step", (0.1, 0.0, 1.0 / 1024.0, 1.0 / 512.0), 100), "RGBA"),
    ]
    shading = custom(material, "ash_advection.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, MATERIAL_DIR, "M_AshAdvection")


def build_fx(name, mode, shape, two_sided):
    material = create_material(FX_MATERIAL_DIR, name)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", two_sided)
    vertex_color = node(material, unreal.MaterialExpressionVertexColor, -900, 100)
    inputs = [
        ("NormalWS", node(material, unreal.MaterialExpressionVertexNormalWS, -900, -200), ""),
        ("CamVec", node(material, unreal.MaterialExpressionCameraVectorWS, -900, -100), ""),
        ("UV", node(material, unreal.MaterialExpressionTextureCoordinate, -900, 0), ""),
        ("VertexRGB", vertex_color, ""),
        ("VertexA", vertex_color, "A"),
        ("LocalPos", local_position(material, 500), ""),
        ("Time", node(material, unreal.MaterialExpressionTime, -900, 600), ""),
        ("Tint", vector_parameter(material, "Tint", (1.0, 0.6, 0.25, 4.0), 200), "RGBA"),
        ("Shape", vector_parameter(material, "Shape", shape, 300), "RGBA"),
        ("Mode", constant4(material, (float(mode), 0.0, 0.0, 0.0), 400), ""),
    ]
    shading = custom(material, "fx.hlsl", inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    output(shading, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, FX_MATERIAL_DIR, name)


def build_projectile_body():
    material = create_material(FX_MATERIAL_DIR, "M_ProjectileBody")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Unlit with a cheap view-facing sheen: the hull reads as metal without
    # depending on scene lights that the unlit planet no longer needs.
    base = vector_parameter(material, "BaseColor", (0.30, 0.31, 0.34, 1.0), 0)
    glow = vector_parameter(material, "Emissive", (0.0, 0.0, 0.0, 0.0), 100)
    fresnel = node(material, unreal.MaterialExpressionFresnel, -900, 200)
    fresnel.set_editor_property("exponent", 2.0)
    sheen = node(material, unreal.MaterialExpressionOneMinus, -700, 200)
    connect(fresnel, "", sheen, "")
    shaded = node(material, unreal.MaterialExpressionMultiply, -500, 100)
    connect(base, "RGB", shaded, "A")
    connect(sheen, "", shaded, "B")
    total = node(material, unreal.MaterialExpressionAdd, -300, 100)
    connect(shaded, "", total, "A")
    connect(glow, "RGB", total, "B")
    output(total, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(material, FX_MATERIAL_DIR, "M_ProjectileBody")


def build_niagara_templates():
    # Built-in UE Niagara examples, copied into project content so the game uses
    # stable project paths and never depends on marketplace FX.
    source_root = "/Niagara/DefaultAssets/Templates/Systems/"
    ensure_dir(FX_DIR)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/Niagara/DefaultAssets/Templates/Systems"], force_rescan=False)
    copies = (
        ("SimpleExplosion", "NS_EarthfallImpact"),
        ("FountainLightweight", "NS_EarthfallSmoke"),
        ("FountainLightweight", "NS_EarthfallNukePlume"),
        ("RadialBurst", "NS_EarthfallNukeShockwave"),
        ("FountainLightweight", "NS_EarthfallPlasmaBeam"),
        ("DirectionalBurstLightweight", "NS_EarthfallPlasmaSparks"),
    )
    for source_name, target_name in copies:
        target_path = FX_DIR + "/" + target_name
        if EAL.does_asset_exist(target_path):
            continue
        source_path = source_root + source_name
        if not EAL.does_asset_exist(source_path):
            raise RuntimeError("Missing built-in Niagara template: " + source_path)
        duplicate = EAL.duplicate_asset(source_path, target_path)
        if not duplicate:
            raise RuntimeError("Could not copy Niagara template: " + source_path)
        EAL.save_loaded_asset(duplicate)


def remove_obsolete_assets():
    for path in OBSOLETE_ASSETS:
        if EAL.does_asset_exist(path):
            if EAL.delete_asset(path):
                unreal.log("EARTHFALL removed obsolete asset " + path)
            else:
                unreal.log_warning("EARTHFALL could not remove obsolete asset " + path)


def main():
    for directory in (TEXTURE_DIR, MATERIAL_DIR, FX_MATERIAL_DIR, FX_DIR):
        ensure_dir(directory)
    default = unreal.TextureCompressionSettings.TC_DEFAULT
    day = import_texture("T_Earth_Day.png", "T_Earth_Day", True, default)
    night = import_texture("T_Earth_Night.png", "T_Earth_Night", True, default)
    normal = import_texture("T_Earth_Normal.png", "T_Earth_Normal", False, unreal.TextureCompressionSettings.TC_NORMALMAP)
    masks = import_texture("T_Earth_Masks.png", "T_Earth_Masks", False, unreal.TextureCompressionSettings.TC_MASKS)
    sky = import_texture("NASA_WISE_AllSky_MilkyWay_4096.jpg", "T_Space_Skybox", True, default)
    black = unreal.load_asset(BLACK)
    if not black:
        raise RuntimeError("Missing engine texture " + BLACK)

    build_surface(day, night, normal, masks, black)
    build_clouds(black)
    build_atmosphere()
    build_space(sky)
    build_sun("M_EarthSun", (0.0, 42.0, 0.0, 0.0), additive=False)
    build_sun("M_SunCorona", (1.0, 1.0, 0.0, 0.0), additive=True)
    build_moon()
    build_ash_advection(black)
    build_fx("M_FXGlow", 0, (2.2, 0.8, 3.0, 0.0), two_sided=False)
    build_fx("M_FXTrail", 1, (1.6, 1.2, 0.0, 0.0), two_sided=True)
    build_fx("M_FXRing", 2, (3.0, 1.6, 0.0, 0.0), two_sided=True)
    build_projectile_body()
    build_niagara_templates()
    remove_obsolete_assets()
    EAL.save_directory("/Game/Earth", only_if_is_dirty=True, recursive=True)
    EAL.save_directory("/Game/Earthfall", only_if_is_dirty=True, recursive=True)
    unreal.log("EARTHFALL_ASSET_BUILD_COMPLETE")


if __name__ == "__main__":
    main()
