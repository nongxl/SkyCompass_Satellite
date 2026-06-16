import serial
import serial.tools.list_ports
import base64
import os
import re
import time
import struct
from datetime import datetime

def find_m5stack_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.upper()
        if any(k in desc for k in ["CP210", "USB SERIAL", "USB DEVICE", "ESP32", "M5STACK", "JTAG"]):
            return port.device
        if port.vid == 0x303A:
            return port.device
    return "COM5"

# Regex to strip ESP32 log prefix: [12345][I][main.cpp:123] functionName():
LOG_PREFIX_RE = re.compile(r'^\s*\[\s*\d+\]\[.\]\[.*?\]\s*\w+\(\):\s*')

def strip_log_prefix(line):
    """Strip ESP32 log prefix from a line."""
    return LOG_PREFIX_RE.sub('', line)

def rgb565_to_bmp(data, width, height):
    """Convert raw RGB565 bytes to BMP file bytes."""
    pixels = []
    for i in range(0, len(data), 2):
        if i + 1 >= len(data):
            break
        # M5GFX/LGFX framebuffer is big-endian for SPI DMA compatibility
        pixel = (data[i] << 8) | data[i+1]
        r = ((pixel >> 11) & 0x1F) << 3
        g = ((pixel >> 5) & 0x3F) << 2
        b = (pixel & 0x1F) << 3
        pixels.append((r, g, b))
    
    row_size = (width * 3 + 3) & ~3
    pixel_data_size = row_size * height
    file_size = 54 + pixel_data_size
    
    bmp = bytearray()
    bmp += b'BM'
    bmp += struct.pack('<I', file_size)
    bmp += struct.pack('<HH', 0, 0)
    bmp += struct.pack('<I', 54)
    bmp += struct.pack('<I', 40)
    bmp += struct.pack('<i', width)
    bmp += struct.pack('<i', -height)  # Top-down
    bmp += struct.pack('<HH', 1, 24)
    bmp += struct.pack('<I', 0)
    bmp += struct.pack('<I', pixel_data_size)
    bmp += struct.pack('<ii', 2835, 2835)
    bmp += struct.pack('<II', 0, 0)
    
    for y in range(height):
        row = bytearray()
        for x in range(width):
            idx = y * width + x
            if idx < len(pixels):
                r, g, b = pixels[idx]
                row += bytes([b, g, r])
            else:
                row += bytes([0, 0, 0])
        while len(row) % 4 != 0:
            row += b'\x00'
        bmp += row
    
    return bytes(bmp)

def listen_for_screenshots(target_port=None):
    port_name = target_port if target_port else find_m5stack_port()
    
    print(f"\n--- SkyCompass 截屏监听服务 v3.0 (Log Channel Mode) ---")
    print(f"目标端口: {port_name}")
    print("[*] 提示: 在设备上按 BtnG0 (侧面按钮) 触发截屏")
    print("[*] 烧录前请先按 Ctrl+C 关闭此脚本\n")
    
    while True:
        ser = None
        try:
            ser = serial.Serial(port_name, 115200, timeout=1)
            print(f"[OK] [{datetime.now().strftime('%H:%M:%S')}] 已成功连接到 {port_name}")
            
            collecting = False
            b64_buffer = ""
            img_width = 0
            img_height = 0
            
            while True:
                try:
                    line = ser.readline()
                except serial.SerialException:
                    print(f"\n[WARNING] [{datetime.now().strftime('%H:%M:%S')}] 串口连接断开。")
                    break
                
                if not line:
                    continue
                    
                try:
                    raw_line = line.decode('utf-8', errors='ignore').strip()
                    # Strip log prefix to get clean content
                    clean = strip_log_prefix(raw_line)
                    
                    # 1. Check start marker
                    if "==SKYCOMPASS_RAW_START==" in raw_line:
                        try:
                            # Handle both raw_line or clean
                            dims = raw_line.split("==SKYCOMPASS_RAW_START==")[1]
                            # Strip any ANSI color codes if they exist at the end
                            dims = re.sub(r'\x1b\[[0-9;]*[mK]', '', dims).strip()
                            parts = dims.split(",")
                            img_width = int(parts[0])
                            img_height = int(parts[1])
                        except Exception as parse_err:
                            img_width, img_height = 240, 135
                        print(f"\n[START] [{datetime.now().strftime('%H:%M:%S')}] 开始接收 RGB565 数据 ({img_width}x{img_height})...")
                        collecting = True
                        b64_buffer = ""
                        continue
                    
                    # 2. Check end marker
                    if "==SKYCOMPASS_RAW_END==" in raw_line:
                        if collecting and b64_buffer:
                            expected = img_width * img_height * 2
                            print(f"[*] 接收完成 (Base64长度: {len(b64_buffer)})，正在转换...")
                            try:
                                raw_data = base64.b64decode(b64_buffer)
                                print(f"[*] 解码完成: {len(raw_data)} bytes (期望: {expected})")
                                
                                bmp_data = rgb565_to_bmp(raw_data, img_width, img_height)
                                
                                save_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'screenshot')
                                if not os.path.exists(save_dir):
                                    os.makedirs(save_dir)
                                filename = os.path.join(save_dir, f"skycompass_{datetime.now().strftime('%Y%m%d_%H%M%S')}.bmp")
                                with open(filename, 'wb') as f:
                                    f.write(bmp_data)
                                print(f"[SUCCESS] 截图保存成功: {filename}\n")
                            except Exception as decode_err:
                                print(f"!!! 解码/转换失败: {decode_err}")
                        collecting = False
                        continue
                    
                    # 3. Check data line
                    if collecting and "==SKYCOMPASS_DATA==" in raw_line:
                        try:
                            parts = raw_line.split("==SKYCOMPASS_DATA==")
                            if len(parts) > 1:
                                b64_part = parts[1]
                                # Strip ANSI color escape codes (e.g. \x1b[0m)
                                b64_part = re.sub(r'\x1b\[[0-9;]*[mK]', '', b64_part)
                                # Clean up any remaining non-base64 characters
                                b64_part = re.sub(r'[^A-Za-z0-9+/=]', '', b64_part)
                                b64_buffer += b64_part
                        except Exception as data_err:
                            pass
                    
                    # 4. Standard log output
                    elif "[Screenshot]" in raw_line:
                        # Print status info to console
                        clean_msg = strip_log_prefix(raw_line)
                        print(f"设备消息: {clean_msg}")
                        
                except Exception as e:
                    pass
        
        except serial.SerialException:
            time.sleep(2)
            continue
        except KeyboardInterrupt:
            print("\n服务已通过用户请求停止。")
            if ser and ser.is_open: ser.close()
            return
        finally:
            if ser and ser.is_open: ser.close()

if __name__ == "__main__":
    listen_for_screenshots()
