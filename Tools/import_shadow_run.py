from pathlib import Path
import unreal as u

src = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir())) / "SourceAssets"
dest = "/Game/ShadowRun"
assets = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()

def load_file(path, folder):
    folder = f"{dest}/{folder}"
    package = f"{folder}/{path.stem}"
    if assets.does_asset_exist(package):
        return assets.load_asset(package)
    task = u.AssetImportTask()
    task.filename = str(path)
    task.destination_path = folder
    task.automated = True
    task.save = True
    if path.suffix == ".fbx":
        options = u.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
        options.import_as_skeletal = False
        options.import_materials = "ShadowRun_Assets" in path.parts
        options.import_textures = options.import_materials
        options.static_mesh_import_data.combine_meshes = True
        task.options = options
    tools.import_asset_tasks([task])
    obj = assets.load_asset(package)
    if not obj:
        raise RuntimeError(f"Import did not produce {package}: {task.imported_object_paths}")
    return obj

for path in sorted((src / "ShadowRun_Assets").rglob("*.fbx")):
    load_file(path, "Models/" + path.stem)
for path in sorted((src / "Shadow_Run_Sound_Effects").rglob("*.wav")):
    load_file(path, "Audio")
for path in sorted((src / "Shadow_Run_UI_Assets" / "PNG").glob("*.png")):
    tex = load_file(path, "UI")
    tex.compression_settings = u.TextureCompressionSettings.TC_EDITOR_ICON
    tex.lod_group = u.TextureGroup.TEXTUREGROUP_UI
    assets.save_loaded_asset(tex, only_if_is_dirty=True)
for path in sorted((src / "Shadow_Run_Texturas_PBR").glob("*/*.png")):
    tex = load_file(path, "PBR/" + path.parent.name)
    if path.stem.endswith("_Normal"):
        tex.compression_settings = u.TextureCompressionSettings.TC_NORMALMAP
        tex.flip_green_channel = True
    tex.srgb = path.stem.endswith("_BaseColor")
    assets.save_loaded_asset(tex, only_if_is_dirty=True)
for path in sorted((src / "Shadow_Run_VFX_Kit" / "Texturas").glob("*.png")):
    load_file(path, "VFX/Texturas")
for path in sorted((src / "Shadow_Run_VFX_Kit" / "Malhas").glob("*.fbx")):
    load_file(path, "VFX/Malhas")

map_path = dest + "/Maps/L_Industrial"
if not assets.does_asset_exist(map_path):
    world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world or not u.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("Could not create industrial map")
u.log("SHADOW_RUN_IMPORT_COMPLETE")