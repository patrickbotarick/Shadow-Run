"""Build weapon materials and import a supplied pistol GLB for the prototype."""
from pathlib import Path
import sys
import unreal as ue

ASSETS = ue.EditorAssetLibrary
EDIT = ue.MaterialEditingLibrary
TOOLS = ue.AssetToolsHelpers.get_asset_tools()
FOLDER = "/Game/ShadowRun/Weapons"


def material(name):
    path = f"{FOLDER}/{name}"
    if ASSETS.does_asset_exist(path):
        asset = ASSETS.load_asset(path)
        EDIT.delete_all_material_expressions(asset)
        return asset
    return TOOLS.create_asset(name, FOLDER, ue.Material, ue.MaterialFactoryNew())


def node(mat, cls, x, y, **values):
    expr = EDIT.create_material_expression(mat, cls, x, y)
    for key, value in values.items():
        expr.set_editor_property(key, value)
    return expr


def wire(source, output, dest, slot):
    if not EDIT.connect_material_expressions(source, output, dest, slot):
        raise RuntimeError(f"Failed to connect {slot}")


def output(expr, pin, prop):
    if not EDIT.connect_material_property(expr, pin, prop):
        raise RuntimeError(f"Failed to connect {prop}")


def save(mat):
    EDIT.recompile_material(mat)
    if not ASSETS.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {mat.get_path_name()}")


rifle = material("M_CarbineGunmetal")
texture = ASSETS.load_asset("/Game/ShadowRun/Models/futuristic_gun_3d_model/futuristic_gun_3d_model_basecolor")
if not texture:
    raise RuntimeError("Missing original carbine texture")
sample = node(rifle, ue.MaterialExpressionTextureSample, -700, 0, texture=texture,
              sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_COLOR)
tint = node(rifle, ue.MaterialExpressionConstant3Vector, -700, 250,
            constant=ue.LinearColor(.32, .38, .43))
multiply = node(rifle, ue.MaterialExpressionMultiply, -410, 0)
wire(sample, "RGB", multiply, "A")
wire(tint, "", multiply, "B")
output(multiply, "", ue.MaterialProperty.MP_BASE_COLOR)
for row, (prop, value) in enumerate(((ue.MaterialProperty.MP_METALLIC, .56),
                                     (ue.MaterialProperty.MP_ROUGHNESS, .41))):
    scalar = node(rifle, ue.MaterialExpressionConstant, -370, 220 + row * 120, r=value)
    output(scalar, "", prop)
save(rifle)

for name, tex in (("M_ShotFlash", "Flash_Tiro"), ("M_ShotTrace", "Tracante"),
                  ("M_ShotImpact", "Impacto_Metal"), ("M_DroneFlash", "Flare_Energia")):
    mat = material(name)
    mat.set_editor_property("blend_mode", ue.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    sprite = ASSETS.load_asset(f"/Game/ShadowRun/VFX/Texturas/T_VFX_{tex}")
    image = node(mat, ue.MaterialExpressionTextureSample, -760, -60, texture=sprite,
                 sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_COLOR)
    color = node(mat, ue.MaterialExpressionVectorParameter, -760, 240, parameter_name="ShotColor",
                 default_value=ue.LinearColor(1, .45, .12, 1))
    masked = node(mat, ue.MaterialExpressionMultiply, -480, -60)
    wire(image, "RGB", masked, "A")
    wire(image, "A", masked, "B")
    colored = node(mat, ue.MaterialExpressionMultiply, -260, -60)
    wire(masked, "", colored, "A")
    wire(color, "", colored, "B")
    output(colored, "", ue.MaterialProperty.MP_EMISSIVE_COLOR)
    save(mat)

if len(sys.argv) > 1:
    source = Path(sys.argv[1]).resolve(strict=True)
    package = f"{FOLDER}/Pistol/SM_TestPistol"
    if not ASSETS.does_asset_exist(package):
        task = ue.AssetImportTask()
        task.filename = str(source)
        task.destination_path = FOLDER + "/Pistol"
        task.automated = True
        task.save = True
        TOOLS.import_asset_tasks([task])
        ue.log(f"SHADOW_RUN_PISTOL_IMPORT {list(task.imported_object_paths)}")
        meshes = [path for path in task.imported_object_paths if isinstance(ASSETS.load_asset(path), ue.StaticMesh)]
        if len(meshes) != 1:
            raise RuntimeError("Pistol import produced no static mesh")
        if meshes[0].split(".")[0] != package and not ASSETS.rename_asset(meshes[0], package):
            raise RuntimeError("Could not assign the pistol asset path")
    mesh = ASSETS.load_asset(package)
    ue.log(f"SHADOW_RUN_PISTOL_BOUNDS {mesh.get_bounding_box()}")

ue.log("SHADOW_RUN_WEAPONS_READY")
