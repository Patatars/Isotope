from __future__ import annotations

import json
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
SOURCE_DIR = PROJECT_DIR / "SourceArt" / "UI" / "Keys"
MANIFEST_PATH = SOURCE_DIR / "glyph_manifest.json"
DESTINATION_PATH = "/Game/UI/Textures/Keys"
CONTROLLER_DATA_PATH = "/Game/UI/InputData/CID_KeyboardMouse"
BRUSH_SIZE = unreal.DeprecateSlateVector2D()
BRUSH_SIZE.set_editor_property("x", 64.0)
BRUSH_SIZE.set_editor_property("y", 64.0)


def import_textures(manifest: list[dict[str, str]]) -> dict[str, unreal.Texture2D]:
    tasks = []
    for item in manifest:
        asset_name = Path(item["file"]).stem
        if unreal.EditorAssetLibrary.does_asset_exist(f"{DESTINATION_PATH}/{asset_name}"):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(SOURCE_DIR / item["file"]))
        task.set_editor_property("destination_path", DESTINATION_PATH)
        task.set_editor_property("destination_name", asset_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)

    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = {}
    for item in manifest:
        asset_name = Path(item["file"]).stem
        texture = unreal.load_asset(f"{DESTINATION_PATH}/{asset_name}")
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"Texture import failed: {asset_name}")
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("never_stream", True)
        texture.modify()
        textures[item["key"]] = texture

    return textures


def configure_controller_data(
    manifest: list[dict[str, str]],
    textures: dict[str, unreal.Texture2D],
) -> None:
    blueprint = unreal.load_asset(CONTROLLER_DATA_PATH)
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Controller Data Blueprint not found: {CONTROLLER_DATA_PATH}")

    generated_class = blueprint.generated_class()
    controller_data = unreal.get_default_object(generated_class)
    configurations = []

    for item in manifest:
        key = unreal.Key()
        key.set_editor_property("key_name", unreal.Name(item["key"]))
        brush = unreal.SlateBrush(
            image_size=BRUSH_SIZE,
            resource_object=textures[item["key"]],
            draw_as=unreal.SlateBrushDrawType.IMAGE,
        )
        configuration = unreal.CommonInputKeyBrushConfiguration()
        configuration.set_editor_property("key", key)
        configuration.set_editor_property("key_brush", brush)
        configurations.append(configuration)

    with unreal.ScopedEditorTransaction("Configure Common Input Keyboard Glyphs"):
        controller_data.modify()
        controller_data.set_editor_property("input_type", unreal.CommonInputType.MOUSE_AND_KEYBOARD)
        controller_data.set_editor_property("input_brush_data_map", configurations)
        blueprint.modify()
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_directory(DESTINATION_PATH, only_if_is_dirty=False, recursive=True)


def main() -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    textures = import_textures(manifest)
    unreal.EditorAssetLibrary.save_directory(
        DESTINATION_PATH,
        only_if_is_dirty=False,
        recursive=True,
    )
    configure_controller_data(manifest, textures)
    unreal.log(f"Configured {len(manifest)} CommonInput keyboard glyphs")


main()
