"""Execute no Unreal Editor com Python Editor Script Plugin ativado."""
from pathlib import Path
import unreal as ue

FOLDER = "/Game/ShadowRun/VFX"
SOURCE = Path(__file__).resolve().parent.parent / "Texturas"
TOOLS = ue.AssetToolsHelpers.get_asset_tools()
ASSETS = ue.EditorAssetLibrary
EDIT = ue.MaterialEditingLibrary
PROPS = ue.MaterialProperty

def texture(filename):
    path = f"{FOLDER}/Texturas/{filename}"
    if not ASSETS.does_asset_exist(path):
        task = ue.AssetImportTask()
        task.set_editor_property("filename", str(SOURCE / (filename + ".png")))
        task.set_editor_property("destination_path", FOLDER + "/Texturas")
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        TOOLS.import_asset_tasks([task])
    result = ASSETS.load_asset(path)
    if not isinstance(result, ue.Texture2D):
        raise RuntimeError(f"Textura não importada: {path}")
    return result

def new_material(name, blend, unlit=True, two_sided=True):
    path = f"{FOLDER}/Materiais/{name}"
    if ASSETS.does_asset_exist(path):
        ue.log(f"Preservado: {path}")
        return None
    m = TOOLS.create_asset(name,FOLDER + "/Materiais",ue.Material,ue.MaterialFactoryNew())
    if not m:
        raise RuntimeError(f"Falha ao criar {path}")
    m.set_editor_property("blend_mode",blend)
    if unlit:
        m.set_editor_property("shading_model",ue.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided",two_sided)
    return m

def node(m,cls,x,y,**props):
    value = EDIT.create_material_expression(m,cls,x,y)
    for prop,data in props.items():
        value.set_editor_property(prop,data)
    return value

def multiply(m, first, second, x, y, pin_a="", pin_b=""):
    value = node(m,ue.MaterialExpressionMultiply,x,y)
    if not EDIT.connect_material_expressions(first,pin_a,value,"A"):
        raise RuntimeError("Falha de conexão na entrada A")
    if not EDIT.connect_material_expressions(second,pin_b,value,"B"):
        raise RuntimeError("Falha de conexão na entrada B")
    return value

def connect(expr, prop):
    if not EDIT.connect_material_property(expr,"",prop):
        raise RuntimeError(f"Falha de conexão em {prop}")

def scalar(m,name,value,x,y):
    return node(m,ue.MaterialExpressionScalarParameter,x,y,parameter_name=name,default_value=value)

def tint(m,x,y,rgba=(1.,1.,1.,1.)):
    return node(m,ue.MaterialExpressionVectorParameter,x,y,parameter_name="Tint",
                default_value=ue.LinearColor(*rgba))

def finish(m):
    EDIT.recompile_material(m)
    ASSETS.save_loaded_asset(m,only_if_is_dirty=False)

def sprite(name, tex, smoke):
    blend=ue.BlendMode.BLEND_TRANSLUCENT if smoke else ue.BlendMode.BLEND_ADDITIVE
    m=new_material(name,blend)
    if not m:
        return
    sample=node(m,ue.MaterialExpressionTextureSampleParameter2D,-1140,-400,
                parameter_name="SpriteTexture",texture=tex)
    particle=node(m,ue.MaterialExpressionParticleColor,-1140,100)
    color=tint(m,-1140,-70)
    gain=scalar(m,"Intensity",.8 if smoke else 3.,-1140,-220)
    opacity=scalar(m,"Opacity",.75 if smoke else 1.,-1140,380)
    rgb=multiply(m,sample,color,-810,-400,"RGB")
    rgb=multiply(m,rgb,particle,-630,-400,"","RGB")
    rgb=multiply(m,rgb,gain,-450,-400)
    alpha=multiply(m,sample,particle,-800,100,"A","A")
    alpha=multiply(m,alpha,opacity,-610,100)
    if smoke:
        connect(rgb,PROPS.MP_EMISSIVE_COLOR)
        connect(alpha,PROPS.MP_OPACITY)
    else:
        light=multiply(m,rgb,alpha,-250,-390)
        connect(light,PROPS.MP_EMISSIVE_COLOR)
    finish(m)

def shield():
    m=new_material("M_VFX_Escudo_Fresnel",ue.BlendMode.BLEND_TRANSLUCENT,two_sided=False)
    if not m:
        return
    rim=node(m,ue.MaterialExpressionFresnel,-790,-80)
    color=tint(m,-790,-300,(.016,.79,.89,1.))
    gain=scalar(m,"Intensity",2.5,-790,-440)
    opacity=scalar(m,"Opacity",.48,-790,150)
    glow=multiply(m,color,rim,-520,-300)
    connect(multiply(m,glow,gain,-310,-300),PROPS.MP_EMISSIVE_COLOR)
    connect(multiply(m,rim,opacity,-350,130),PROPS.MP_OPACITY)
    finish(m)

def debris():
    m=new_material("M_VFX_Fragmento_Metal",ue.BlendMode.BLEND_OPAQUE,False,False)
    if not m:
        return
    connect(tint(m,-460,-300,(.11,.13,.14,1.)),PROPS.MP_BASE_COLOR)
    connect(scalar(m,"Metallic",.85,-460,-110),PROPS.MP_METALLIC)
    connect(scalar(m,"Roughness",.50,-460,90),PROPS.MP_ROUGHNESS)
    finish(m)

if not SOURCE.is_dir():
    raise RuntimeError("Extraia o ZIP mantendo as pastas Texturas e Unreal no mesmo nível.")
for filename in sorted(SOURCE.glob("T_VFX_*.png")):
    stem=filename.stem
    is_smoke=stem in {"T_VFX_Flipbook_Fumaca_4x4","T_VFX_Nuvem_Suave"}
    sprite("M_" + stem[2:], texture(stem), is_smoke)
shield()
debris()
ue.log("Shadow Run: materiais VFX criados. Abra os materiais e confirme o resultado no editor.")
