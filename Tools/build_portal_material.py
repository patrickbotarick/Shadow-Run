"""Rebuild the animated ballistic field material in the Unreal Editor."""
import unreal as ue

folder = "/Game/ShadowRun/VFX"
name = "M_PortalField"
assets = ue.EditorAssetLibrary
edit = ue.MaterialEditingLibrary
path = f"{folder}/{name}"
material = assets.load_asset(path) if assets.does_asset_exist(path) else None
if not material:
    material = ue.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, ue.Material, ue.MaterialFactoryNew()
    )
edit.delete_all_material_expressions(material)
material.set_editor_property("blend_mode", ue.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)

field = edit.create_material_expression(material, ue.MaterialExpressionCustom, 0, 0)
field.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT4)
inputs = []
for pin in ("UV", "Time", "Impact"):
    entry = ue.CustomInput()
    entry.set_editor_property("input_name", pin)
    inputs.append(entry)
field.set_editor_property("inputs", inputs)
field.set_editor_property("code", """
float2 p = float2(UV.y, 1.0 - UV.x);
float2 centered = p - 0.5;
float edge = 1.0 - smoothstep(0.0, 0.12, 0.5 - abs(centered.x));
float cap = 1.0 - smoothstep(0.0, 0.09, 0.5 - abs(centered.y));
float phase = p.y * 7.0 - Time * 0.85 + sin(p.x * 8.0 + Time * 1.7) * 0.35;
float interference = pow(0.5 + 0.5 * sin(phase * 6.283185), 5);
float scan = 1.0 - smoothstep(0.0, 0.07, abs(p.y - frac(Time * 0.46)));
float d = length((p - Impact.xy) * float2(1.38, 1.0));
float age = max(0.0, Time - Impact.z);
float ripple = (1.0 - smoothstep(0.0, 0.035, abs(d - age * 1.1)))
    * saturate(1.0 - age * 1.8) * Impact.w;
float glow = 0.24 + 0.43 * edge + 0.16 * cap + 0.18 * interference + 0.45 * scan + 2.0 * ripple;
float opacity = saturate(0.07 + 0.12 * edge + 0.07 * cap + 0.08 * interference + 0.13 * scan + 0.52 * ripple);
return float4(float3(0.02, 0.63, 1.05) * glow, opacity);
""")

uv = edit.create_material_expression(material, ue.MaterialExpressionTextureCoordinate, -450, -120)
clock = edit.create_material_expression(material, ue.MaterialExpressionScalarParameter, -450, 0)
clock.set_editor_property("parameter_name", "FieldTime")
impact = edit.create_material_expression(material, ue.MaterialExpressionVectorParameter, -450, 140)
impact.set_editor_property("parameter_name", "Impact")
impact.set_editor_property("default_value", ue.LinearColor(0, 0, 0, 0))
for source, output, pin in ((uv, "", "UV"), (clock, "", "Time"), (impact, "RGBA", "Impact")):
    if not edit.connect_material_expressions(source, output, field, pin):
        raise RuntimeError(f"Failed to connect {pin}")

for channels, prop in (("rgb", ue.MaterialProperty.MP_EMISSIVE_COLOR),
                       ("a", ue.MaterialProperty.MP_OPACITY)):
    mask = edit.create_material_expression(material, ue.MaterialExpressionComponentMask, 350, 0)
    for channel in channels:
        mask.set_editor_property(channel, True)
    if not edit.connect_material_expressions(field, "", mask, ""):
        raise RuntimeError(f"Failed to connect {channels}")
    if not edit.connect_material_property(mask, "", prop):
        raise RuntimeError(f"Failed to connect {prop}")

edit.recompile_material(material)
if not assets.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError("Failed to save portal field")
ue.log("SHADOW_RUN_PORTAL_MATERIAL_READY")
