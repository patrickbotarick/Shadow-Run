"""Create the material assets used by the industrial sector in Unreal Editor."""
import unreal as ue


FOLDER = "/Game/ShadowRun/Sector"
ASSETS = ue.EditorAssetLibrary
TOOLS = ue.AssetToolsHelpers.get_asset_tools()
EDIT = ue.MaterialEditingLibrary


def material(name):
    package = f"{FOLDER}/{name}"
    if ASSETS.does_asset_exist(package):
        return ASSETS.load_asset(package), False
    asset = TOOLS.create_asset(name, FOLDER, ue.Material, ue.MaterialFactoryNew())
    if not asset:
        raise RuntimeError(f"Could not create {package}")
    return asset, True


def connect(expression, output, prop):
    if not EDIT.connect_material_property(expression, output, prop):
        raise RuntimeError(f"Could not connect {prop}")


def save(asset):
    EDIT.set_base_material_usage(asset, ue.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
    EDIT.recompile_material(asset)
    if not ASSETS.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def surface(name, texture_folder, prefix):
    asset, created = material(name)
    if not created:
        EDIT.delete_all_material_expressions(asset)
    maps = (
        ("BaseColor", ue.MaterialSamplerType.SAMPLERTYPE_COLOR, ue.MaterialProperty.MP_BASE_COLOR, "RGB"),
        ("Normal", ue.MaterialSamplerType.SAMPLERTYPE_NORMAL, ue.MaterialProperty.MP_NORMAL, "RGB"),
        ("Metallic", ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, ue.MaterialProperty.MP_METALLIC, "R"),
        ("Roughness", ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, ue.MaterialProperty.MP_ROUGHNESS, "R"),
    )
    for row, (suffix, sampler, prop, output) in enumerate(maps):
        texture = ASSETS.load_asset(f"/Game/ShadowRun/PBR/{texture_folder}/{prefix}_{suffix}")
        if not texture:
            raise RuntimeError(f"Missing {prefix}_{suffix}")
        if suffix in ("Metallic", "Roughness") and texture.get_editor_property("srgb"):
            texture.set_editor_property("srgb", False)
            if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
                raise RuntimeError(f"Could not save linear data texture {prefix}_{suffix}")
        sample = EDIT.create_material_expression(asset, ue.MaterialExpressionTextureSample, -450, row * 230)
        sample.set_editor_property("texture", texture)
        sample.set_editor_property("sampler_type", sampler)
        connect(sample, output, prop)
    save(asset)


def flat(name, rgb, roughness, metal=0, glow=0):
    asset, created = material(name)
    if not created:
        save(asset)
        return
    color = EDIT.create_material_expression(asset, ue.MaterialExpressionConstant3Vector, -370, 0)
    color.set_editor_property("constant", ue.LinearColor(*rgb))
    connect(color, "", ue.MaterialProperty.MP_BASE_COLOR)
    for row, (prop, amount) in enumerate(((ue.MaterialProperty.MP_ROUGHNESS, roughness),
                                          (ue.MaterialProperty.MP_METALLIC, metal))):
        scalar = EDIT.create_material_expression(asset, ue.MaterialExpressionConstant, -370, 170 + 130 * row)
        scalar.set_editor_property("r", amount)
        connect(scalar, "", prop)
    if glow:
        emission = EDIT.create_material_expression(asset, ue.MaterialExpressionConstant3Vector, -370, 430)
        emission.set_editor_property("constant", ue.LinearColor(*(value * glow for value in rgb)))
        connect(emission, "", ue.MaterialProperty.MP_EMISSIVE_COLOR)
    save(asset)


surface("M_SectorFloor", "08_Piso_Modular", "Piso_Modular")
surface("M_SectorWall", "07_Parede_Modular", "Parede_Modular")
flat("M_SectorTrim", (.045, .057, .069), .53, .54)
flat("M_SectorCeiling", (.092, .112, .135), .75, .26)
flat("M_SectorWarning", (.87, .35, .036), .62, .18, .18)
flat("M_SectorCyan", (.025, .62, .76), .39, .25, 2.3)
flat("M_SectorAmber", (.95, .36, .045), .39, .25, 1.75)
flat("M_SectorWhite", (.66, .81, .94), .41, .21, 2.1)
ue.log("SHADOW_RUN_SECTOR_MATERIALS_READY")
