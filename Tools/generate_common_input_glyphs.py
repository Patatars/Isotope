from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


GLYPHS = {
    **{letter: letter for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
    **{str(number): str(number) for number in range(10)},
    **{f"F{number}": f"F{number}" for number in range(1, 13)},
    "Escape": "ESC",
    "Tab": "TAB",
    "CapsLock": "CAPS",
    "LeftShift": "SHIFT",
    "RightShift": "SHIFT",
    "LeftControl": "CTRL",
    "RightControl": "CTRL",
    "LeftAlt": "ALT",
    "RightAlt": "ALT",
    "SpaceBar": "SPACE",
    "Enter": "ENTER",
    "BackSpace": "BACK",
    "Insert": "INS",
    "Delete": "DEL",
    "Home": "HOME",
    "End": "END",
    "PageUp": "PG UP",
    "PageDown": "PG DN",
    "Up": "↑",
    "Down": "↓",
    "Left": "←",
    "Right": "→",
    "Hyphen": "−",
    "Equals": "=",
    "LeftBracket": "[",
    "RightBracket": "]",
    "Backslash": "\\",
    "Semicolon": ";",
    "Apostrophe": "'",
    "Comma": ",",
    "Period": ".",
    "Slash": "/",
    "Tilde": "~",
    "NumPadZero": "NUM 0",
    "NumPadOne": "NUM 1",
    "NumPadTwo": "NUM 2",
    "NumPadThree": "NUM 3",
    "NumPadFour": "NUM 4",
    "NumPadFive": "NUM 5",
    "NumPadSix": "NUM 6",
    "NumPadSeven": "NUM 7",
    "NumPadEight": "NUM 8",
    "NumPadNine": "NUM 9",
    "Multiply": "NUM ×",
    "Add": "NUM +",
    "Subtract": "NUM −",
    "Decimal": "NUM .",
    "Divide": "NUM /",
    "NumLock": "NUM",
    "LeftMouseButton": "LMB",
    "RightMouseButton": "RMB",
    "MiddleMouseButton": "MMB",
    "ThumbMouseButton": "M4",
    "ThumbMouseButton2": "M5",
    "MouseWheelUp": "MW ↑",
    "MouseWheelDown": "MW ↓",
}


def make_transparent_base(source: Image.Image) -> Image.Image:
    rgb = source.convert("RGB")
    pixels = rgb.load()
    mask = Image.new("L", rgb.size, 0)
    mask_pixels = mask.load()

    for y in range(rgb.height):
        for x in range(rgb.width):
            red, green, blue = pixels[x, y]
            luminance = (red * 30 + green * 59 + blue * 11) // 100
            is_button = luminance < 115 or (blue - red > 9 and blue - green > 3)
            mask_pixels[x, y] = 255 if is_button else 0

    mask = mask.filter(ImageFilter.GaussianBlur(1.2))
    bounds = mask.getbbox()
    if bounds is None:
        raise RuntimeError("Could not find the button silhouette in the generated base image")

    left, top, right, bottom = bounds
    width = right - left
    height = bottom - top
    side = max(width, height)
    center_x = (left + right) // 2
    center_y = (top + bottom) // 2
    crop_box = (
        center_x - side // 2,
        center_y - side // 2,
        center_x - side // 2 + side,
        center_y - side // 2 + side,
    )

    button = rgb.crop(crop_box).convert("RGBA")
    button.putalpha(mask.crop(crop_box))
    return button.resize((512, 512), Image.Resampling.LANCZOS)


def font_for_label(font_path: Path, label: str) -> ImageFont.FreeTypeFont:
    if len(label) == 1:
        size = 245
    elif len(label) <= 3:
        size = 146
    elif len(label) <= 5:
        size = 102
    else:
        size = 82
    return ImageFont.truetype(str(font_path), size=size)


def render_glyph(base: Image.Image, font_path: Path, label: str) -> Image.Image:
    glyph = base.copy()
    font = font_for_label(font_path, label)
    draw = ImageDraw.Draw(glyph)
    bounds = draw.textbbox((0, 0), label, font=font, stroke_width=2)
    text_width = bounds[2] - bounds[0]
    text_height = bounds[3] - bounds[1]
    x = (glyph.width - text_width) / 2 - bounds[0]
    y = (glyph.height - text_height) / 2 - bounds[1] - 8

    shadow = Image.new("RGBA", glyph.size, (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(shadow)
    shadow_draw.text(
        (x + 4, y + 8),
        label,
        font=font,
        fill=(0, 0, 0, 205),
        stroke_width=4,
        stroke_fill=(0, 0, 0, 185),
    )
    shadow = shadow.filter(ImageFilter.GaussianBlur(3.0))
    glyph.alpha_composite(shadow)

    draw = ImageDraw.Draw(glyph)
    draw.text(
        (x, y),
        label,
        font=font,
        fill=(248, 249, 252, 255),
        stroke_width=2,
        stroke_fill=(220, 225, 235, 255),
    )
    return glyph.resize((256, 256), Image.Resampling.LANCZOS)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--font", required=True, type=Path)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    base = make_transparent_base(Image.open(args.base))
    base.save(args.output / "T_Key_Base.png")

    manifest = []
    for key_name, label in GLYPHS.items():
        filename = f"T_Key_{key_name}.png"
        render_glyph(base, args.font, label).save(args.output / filename)
        manifest.append({"key": key_name, "label": label, "file": filename})

    (args.output / "glyph_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(f"Generated {len(manifest)} glyphs in {args.output}")


if __name__ == "__main__":
    main()
