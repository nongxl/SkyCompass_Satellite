import re
import os
from PIL import Image

def load_pixels(filename, expected_count):
    print(f"Loading {filename}...")
    with open(filename, 'r', encoding='utf-8') as f:
        text = f.read()
    tokens = re.findall(r'0x[0-9a-fA-F]+', text)
    if len(tokens) != expected_count:
        raise ValueError(f"Expected {expected_count} tokens in {filename}, got {len(tokens)}")
    return [int(t, 16) for t in tokens]

def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def process_120x120(filename):
    vals = load_pixels(filename, 120 * 120)
    rgb565_list = []
    for val in vals:
        r = (val >> 16) & 0xFF
        g = (val >> 8) & 0xFF
        b = val & 0xFF
        rgb565_list.append(to_rgb565(r, g, b))
    return rgb565_list

def process_large_image(filename, src_w, src_h, target_w=120, target_h=120):
    vals = load_pixels(filename, src_w * src_h)
    print(f"Creating Pillow image for {filename} ({src_w}x{src_h})...")
    img = Image.new('RGB', (src_w, src_h))
    pixels = []
    for val in vals:
        r = (val >> 16) & 0xFF
        g = (val >> 8) & 0xFF
        b = val & 0xFF
        pixels.append((r, g, b))
    img.putdata(pixels)
    
    print(f"Resizing to {target_w}x{target_h} with Lanczos...")
    resized = img.resize((target_w, target_h), Image.Resampling.LANCZOS)
    
    rgb565_list = []
    for r, g, b in resized.getdata():
        rgb565_list.append(to_rgb565(r, g, b))
    return rgb565_list

def format_c_array(name, data_list):
    lines = [f"// 120x120 RGB565 bitmap for {name}", f"const uint16_t {name}[14400] PROGMEM = {{"]
    chunk_size = 16
    for i in range(0, len(data_list), chunk_size):
        chunk = data_list[i:i+chunk_size]
        hex_str = ", ".join(f"0x{val:04X}" for val in chunk)
        if i + chunk_size < len(data_list):
            hex_str += ","
        lines.append("  " + hex_str)
    lines.append("};")
    lines.append("")
    return "\n".join(lines)

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cap_path = os.path.join(base_dir, "cap-1262.txt")
    gps_path = os.path.join(base_dir, "gpsv11.txt")
    chain_path = os.path.join(base_dir, "chain_mono.txt")
    servo_path = os.path.join(base_dir, "unit8servos.txt")

    cap_data = process_120x120(cap_path)
    gps_data = process_120x120(gps_path)
    chain_data = process_large_image(chain_path, 1200, 1200, 120, 120)
    servo_data = process_large_image(servo_path, 800, 800, 120, 120)

    header_content = """#ifndef HARDWARE_MODULE_IMAGES_H
#define HARDWARE_MODULE_IMAGES_H

#include <Arduino.h>

#define HW_IMG_WIDTH  120
#define HW_IMG_HEIGHT 120

"""
    header_content += format_c_array("img_hw_cap_lora1262", cap_data)
    header_content += format_c_array("img_hw_unit_gpsv11", gps_data)
    header_content += format_c_array("img_hw_chain_mono", chain_data)
    header_content += format_c_array("img_hw_unit_8servos", servo_data)
    header_content += "#endif // HARDWARE_MODULE_IMAGES_H\n"

    out_path = os.path.join(base_dir, "src", "core", "hardware_module_images.h")
    print(f"Writing output to {out_path}...")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header_content)
    print("Done! Total bytes:", os.path.getsize(out_path))

if __name__ == "__main__":
    main()
