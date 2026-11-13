"""Rebuild the first-person weapon finishes from the imported color maps."""
import unreal as ue

assets = ue.EditorAssetLibrary
edit = ue.MaterialEditingLibrary
folder = "/Game/ShadowRun/Weapons"
finishes = {
    "M_CarbineGunmetal": (
        "/Game/ShadowRun/Models/futuristic_gun_3d_model/futuristic_gun_3d_model_basecolor",
        ue.LinearColor(.055, .066, .075, 1),
    ),
    "M_PistolGunmetal": (
        f"{folder}/Pistol/model/Textures/Color_34f03b8d-bd0d-4e77-a950-3e1c3b132de8",
        ue.LinearColor(.021, .029, .038, 1),
    ),
}

for name, (source, steel) in finishes.items():
    path = f"{folder}/{name}"
    material = assets.load_asset(path) if assets.does_asset_exist(path) else (
        ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())
    )
    texture = assets.load_asset(source)
    if not material or not texture:
        raise RuntimeError(f"Missing material or source texture for {name}")
    edit.set_material_usage(material, ue.MaterialUsage.MATUSAGE_NANITE)
    edit.delete_all_material_expressions(material)
    color = edit.create_material_expression(material, ue.MaterialExpressionTextureSample, -500, -140)
    color.set_editor_property("texture", texture)
    color.set_editor_property("sampler_type", ue.MaterialSamplerType.SAMPLERTYPE_COLOR)
    uv = edit.create_material_expression(material, ue.MaterialExpressionTextureCoordinate, -500, 120)
    tint = edit.create_material_expression(material, ue.MaterialExpressionConstant3Vector, -500, 260)
    tint.set_editor_property("constant", steel)
    finish = edit.create_material_expression(material, ue.MaterialExpressionCustom, -210, 0)
    finish.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for pin in ("Paint", "UV", "Steel"):
        entry = ue.CustomInput()
        entry.set_editor_property("input_name", pin)
        inputs.append(entry)
    finish.set_editor_property("inputs", inputs)
    finish.set_editor_property("code", """
float value = saturate(dot(Paint, float3(0.2126, 0.7152, 0.0722)));
float warm = smoothstep(0.16, 0.43, Paint.r - Paint.b);
float brush = sin(UV.x * 380.0 + sin(UV.y * 52.0) * 2.0)
            * sin(UV.y * 127.0 + UV.x * 9.0);
float wear = smoothstep(0.70, 0.99, value);
float3 alloy = Steel * (0.20 + 0.93 * sqrt(value) + 0.08 * brush);
alloy += wear * float3(0.014, 0.019, 0.025);
float3 accent = Paint * float3(0.34, 0.25, 0.16);
float roughness = clamp(0.77 - 0.15 * wear + 0.05 * brush, 0.57, 0.84);
return float4(lerp(alloy, accent, warm * 0.58), roughness);
""")
    for node, output, pin in ((color, "RGB", "Paint"), (uv, "", "UV"), (tint, "", "Steel")):
        if not edit.connect_material_expressions(node, output, finish, pin):
            raise RuntimeError(f"Could not connect {name}: {pin}")
    for channels, prop in (("rgb", ue.MaterialProperty.MP_BASE_COLOR), ("a", ue.MaterialProperty.MP_ROUGHNESS)):
        mask = edit.create_material_expression(material, ue.MaterialExpressionComponentMask, 110, 0)
        for channel in channels:
            mask.set_editor_property(channel, True)
        if not edit.connect_material_expressions(finish, "", mask, "") or not edit.connect_material_property(mask, "", prop):
            raise RuntimeError(f"Could not connect {name}: {channels}")
    for prop, value in ((ue.MaterialProperty.MP_METALLIC, .38), (ue.MaterialProperty.MP_SPECULAR, .24)):
        scalar = edit.create_material_expression(material, ue.MaterialExpressionConstant, 120, 220)
        scalar.set_editor_property("r", value)
        if not edit.connect_material_property(scalar, "", prop):
            raise RuntimeError(f"Could not connect {name}: {prop}")
    edit.recompile_material(material)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {name}")

ue.log("SHADOW_RUN_WEAPON_FINISHES_READY")
