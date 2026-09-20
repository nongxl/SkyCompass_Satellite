#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SkyCompass Satellite - 硬件接线示意图与电气拓扑生成器
生成清晰、专业、高可读性的硬件接线示意图 docs/schematic_diagram.png
"""

import os
from PIL import Image, ImageDraw, ImageFont

def get_font(size, bold=False):
    font_paths = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/System/Library/Fonts/PingFang.ttc"
    ]
    for p in font_paths:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()

def draw_rounded_rect(draw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)

def draw_badge(draw, x, y, text, font, bg_color, text_color, pad_x=8, pad_y=3, radius=4, border_color=None):
    bbox = font.getbbox(text)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    box = [x, y, x + tw + pad_x * 2, y + th + pad_y * 2]
    draw_rounded_rect(draw, box, radius=radius, fill=bg_color, outline=border_color, width=1 if border_color else 0)
    draw.text((x + pad_x, y + pad_y - bbox[1]), text, fill=text_color, font=font)
    return box[2]

def generate_schematic():
    W, H = 1760, 1500
    # 采用专业技术深灰色调，告别五颜六色的高饱和度大杂烩
    BG_COLOR = "#0e131b"
    CARD_BG = "#161c26"
    CARD_BORDER = "#252f3e"
    CARD_HEADER_BG = "#1c2432"
    TEXT_MAIN = "#e6edf3"
    TEXT_MUTED = "#8b949e"
    TEXT_ACCENT = "#58a6ff"
    LINE_COLOR = "#212836"

    img = Image.new("RGB", (W, H), BG_COLOR)
    draw = ImageDraw.Draw(img)

    # 字体准备
    title_font = get_font(26, bold=True)
    subtitle_font = get_font(13, bold=False)
    section_font = get_font(17, bold=True)
    card_title_font = get_font(15, bold=True)
    text_bold = get_font(13, bold=True)
    text_normal = get_font(13, bold=False)
    text_small = get_font(11, bold=False)
    tag_font = get_font(11, bold=True)

    # 1. 顶部 Header
    draw.rectangle([0, 0, W, 84], fill="#141923")
    draw.line([(0, 84), (W, 84)], fill=LINE_COLOR, width=2)
    
    # 装饰指示块
    draw.rounded_rectangle([40, 24, 46, 60], radius=3, fill=TEXT_ACCENT)
    
    draw.text((60, 20), "SkyCompass Satellite 硬件接线与电气拓扑示意图", fill=TEXT_MAIN, font=title_font)
    draw.text((62, 53), "Hardware Wiring Topology & Peripheral Pinout Guide (M5Stack Cardputer ADV / v1.1)", fill=TEXT_MUTED, font=subtitle_font)

    # 顶栏硬件标签 (统一沉稳低饱和度配色)
    badges = [
        ("ESP32-S3", "#1f2937", "#9ca3af"),
        ("Cap LoRa-1262", "#1f2937", "#9ca3af"),
        ("Unit 8Servos", "#1f2937", "#9ca3af"),
        ("Chain Mono", "#1f2937", "#9ca3af"),
        ("Unit GPS v1.1", "#1f2937", "#9ca3af"),
    ]
    cur_x = W - 30
    for name, bg, fg in reversed(badges):
        bbox = tag_font.getbbox(name)
        tw = bbox[2] - bbox[0]
        cur_x -= (tw + 18)
        draw_badge(draw, cur_x, 28, name, tag_font, bg, fg, pad_x=7, pad_y=3, radius=4, border_color="#374151")

    # 2. 第一大区：核心模块电气与端口规格 (Module Specs & Pinouts)
    draw.text((40, 100), "一、核心硬件外设与端口电气定义 (Pinout & Port Specifications)", fill=TEXT_ACCENT, font=section_font)

    # Grove 4P 标准线序图例 (简洁黑白灰基底 + 标准线色)
    leg_x = W - 490
    leg_y = 102
    draw.text((leg_x, leg_y), "Grove 4P 线序图例:", fill=TEXT_MUTED, font=tag_font)
    wires = [
        ("黑 GND", "#0b0f17", "#9ca3af", "#30363d"),
        ("红 5V", "#2b1619", "#f87171", "#5c1d24"),
        ("白 SCL/RX", "#22272e", "#e6edf3", "#444c56"),
        ("黄 SDA/TX", "#2d2616", "#f2cc60", "#634f19"),
    ]
    wx = leg_x + 115
    for wname, wbg, wfg, wborder in wires:
        bbox = tag_font.getbbox(wname)
        ww = bbox[2] - bbox[0]
        draw_badge(draw, wx, leg_y - 2, wname, tag_font, wbg, wfg, pad_x=6, pad_y=2, radius=3, border_color=wborder)
        wx += ww + 18

    mod_y = 132
    mod_w = (W - 80 - 36) // 4  # 4 列模块
    mod_h = 244

    mod_configs = [
        {
            "title": "M5 Cardputer 主控",
            "type": "核心运算 / 人机交互 / 六轴IMU",
            "badge": "主控",
            "items": [
                ("顶部 14-Pin Header 扩展槽", "#58a6ff"),
                ("  • GNSS 串口: RX=15, TX=13 (115200)", "#c9d1d9"),
                ("  • I2C 总线: SDA=8, SCL=9 (100kHz)", "#c9d1d9"),
                ("  • SX1262 射频: SPI 总线", "#c9d1d9"),
                ("机身侧面 Grove 接口 (Port A)", "#58a6ff"),
                ("  • 引脚: G1 (RX/SCL), G2 (TX/SDA)", "#c9d1d9"),
                ("  • 供电: 5V (开机自动使能), GND", "#c9d1d9"),
                ("板载外设: MPU6886 六轴姿态感应", "#8b949e"),
                ("快捷键: G0 侧键截图 / M 键硬件向导", "#8b949e"),
            ]
        },
        {
            "title": "Cap LoRa-1262",
            "type": "双频GNSS定位 + 卫星射频遥测",
            "badge": "顶置模块",
            "items": [
                ("安装方式: 顶部 14-Pin 插槽直插", "#58a6ff"),
                ("集成: 双频高精度 GNSS 授时定位", "#c9d1d9"),
                ("  • 占用串口: RX=15, TX=13 (115200)", "#c9d1d9"),
                ("集成: SX1262 业余卫星射频前端", "#c9d1d9"),
                ("  • 433~438MHz 下行遥测 / 多普勒微调", "#c9d1d9"),
                ("顶部扩展: HY2.0-4P 母座 (I2C)", "#58a6ff"),
                ("  • SDA=GPIO8, SCL=GPIO9", "#c9d1d9"),
                ("  • 专供连接 Unit 8Servos 舵机驱动板", "#c9d1d9"),
                ("天线: 配备外置胶棒 / 拉杆天线", "#8b949e"),
            ]
        },
        {
            "title": "Unit 8Servos 驱动板",
            "type": "三轴立体拱门浑仪舵机驱动",
            "badge": "I2C 舵机板",
            "items": [
                ("通信接口: Grove I2C (默认地址 0x25)", "#58a6ff"),
                ("  • 有Cap时: 插接至 Cap 顶部 HY2.0 接口", "#c9d1d9"),
                ("  • 无Cap时: 插接至机身侧面 Grove 接口", "#c9d1d9"),
                ("浑仪三轴舵机通道映射:", "#58a6ff"),
                ("  • CH0: 底座方位走向长梁轴 (0~180°)", "#c9d1d9"),
                ("  • CH1: 天球拱门仰角轴 (30~180°)", "#c9d1d9"),
                ("  • CH2: 拱顶星位滑行指针 (0~180°)", "#c9d1d9"),
                ("供电: 绿色端子必须接 5V DC 独立电源", "#f87171"),
            ]
        },
        {
            "title": "Chain Mono / Unit GPS",
            "type": "8x8点阵副屏 / 侧面定位模块",
            "badge": "Grove外设",
            "items": [
                ("Chain Mono (8x8 单色点阵副屏)", "#58a6ff"),
                ("  • 连接端口: 必须插在 IN 接口 (严禁接OUT)", "#f87171"),
                ("  • 串口通信: UART 115200 (RX=1, TX=2)", "#c9d1d9"),
                ("  • 显示内容: 空间站与卫星过境微动画", "#c9d1d9"),
                ("Unit GPS v1.1 (外置侧面定位)", "#58a6ff"),
                ("  • 机身侧面 Grove 接口直插 (RX=1, TX=2)", "#c9d1d9"),
                ("  • 独占特性: 占用侧面接口，不可与副屏共存", "#8b949e"),
                ("  • 适用场景: 无 Cap 时的便携外置授时", "#8b949e"),
            ]
        }
    ]

    for i, cfg in enumerate(mod_configs):
        bx = 40 + i * (mod_w + 12)
        by = mod_y
        draw_rounded_rect(draw, [bx, by, bx + mod_w, by + mod_h], radius=6, fill=CARD_BG, outline=CARD_BORDER, width=1)
        
        # 顶栏
        draw_rounded_rect(draw, [bx, by, bx + mod_w, by + 34], radius=6, fill=CARD_HEADER_BG)
        draw.rectangle([bx, by + 24, bx + mod_w, by + 34], fill=CARD_HEADER_BG)
        draw.text((bx + 12, by + 8), cfg["title"], fill=TEXT_MAIN, font=card_title_font)
        draw_badge(draw, bx + mod_w - 58, by + 6, cfg["badge"], tag_font, "#212836", "#8b949e", pad_x=4, pad_y=2, radius=3, border_color="#30363d")
        
        # 副标题
        draw.text((bx + 12, by + 40), cfg["type"], fill=TEXT_MUTED, font=text_small)
        draw.line([(bx + 12, by + 57), (bx + mod_w - 12, by + 57)], fill=LINE_COLOR, width=1)
        
        # 列表内容
        iy = by + 65
        for line, color in cfg["items"]:
            draw.text((bx + 12, iy), line, fill=color, font=text_normal)
            iy += 19

    # 3. 第二大区：预设接线场景一览与电气拓扑
    draw.text((40, 396), "二、系统预设硬件接线场景与端口分配 (Preset Hardware Wiring Scenarios)", fill=TEXT_ACCENT, font=section_font)

    # 包含两组方案：分组A（有Cap场景，总线独立无冲突）与 分组B（无Cap场景，侧面接口按需接入）
    sc_cards = [
        # 卡片 1: 顶置 Cap 系列方案
        {
            "group": "组合 A：顶置 Cap LoRa-1262 扩展方案 (外设总线独立，无硬件冲突)",
            "sub": "Cap LoRa 内部独立占用 14-Pin 串口与 SPI 总线，自带 HY2.0 扩展口与侧面 Grove 口相互隔离，可按需自由搭配。",
            "badge": "顶置 Cap 系列",
            "items": [
                ("方案 1: Cap + 浑仪云台 + 像素副屏 (全功能全满血)", 
                 "顶部14P: Cap LoRa  |  Cap顶部HY2.0: Unit 8Servos (I2C)  |  机身侧面Grove: Chain Mono (IN)", 
                 "高精度 GNSS、空间射频接收、三轴浑仪机械指向与 8x8 外显副屏同时并发，各总线互不干扰。"),
                
                ("方案 2: Cap + 浑仪机械云台", 
                 "顶部14P: Cap LoRa  |  Cap顶部HY2.0: Unit 8Servos (I2C)  |  机身侧面Grove: 空闲备用", 
                 "专注于真实物理空间三轴拱门过境追踪，由 Cap 提供离线授时定位与空间遥测。"),
                
                ("方案 3: Cap + 像素副屏 (便携双频外显)", 
                 "顶部14P: Cap LoRa  |  Cap顶部HY2.0: 空闲备用  |  机身侧面Grove: Chain Mono (IN)", 
                 "便携双频 GNSS 定位与空间射频监听，机身侧面外接 8x8 单色点阵副屏渲染动态过境动画。"),
                
                ("方案 4: 仅 Cap 单独使用 (手持便携)", 
                 "顶部14P: Cap LoRa  |  Cap顶部HY2.0: 空闲备用  |  机身侧面Grove: 空闲备用", 
                 "无需任何外挂线缆，整机极其紧凑。内置高精度 GNSS 定位并在屏幕上展示 3D 轨迹与射频遥测。"),
            ]
        },
        # 卡片 2: 侧面 Grove 独立扩展方案 (无 Cap)
        {
            "group": "组合 B：侧面 Grove 独立扩展方案 (无 Cap 场景，侧面接口接入单一外设)",
            "sub": "未扣接 Cap 时，机身侧面 Grove 接口同一时刻仅能接入一个外设，系统自适应分配总线协议与波特率。",
            "badge": "侧面 Grove 系列",
            "items": [
                ("方案 5: 外置 Unit GPS 徒步便携版", 
                 "机身侧面Grove: 插接 Unit GPS v1.1 (UART: RX=1, TX=2)  |  顶部14P / 其它接口: 空闲", 
                 "独占机身侧面 Grove 接口，在户外野外无网络环境下提供高灵敏度陶瓷天线卫星授时定位。"),
                
                ("方案 6: 独立浑仪舵机云台版 (无Cap)", 
                 "机身侧面Grove: 插接 Unit 8Servos (I2C: SDA=2, SCL=1)  |  顶部14P / 其它接口: 空闲", 
                 "机身侧面接口直驱三轴舵机驱动板，系统通过 WiFi NTP 对时或利用断网内置/离线坐标运行。"),
                
                ("方案 7: 独立像素副屏版 (无Cap)", 
                 "机身侧面Grove: 插接 Chain Mono (UART: RX=1, TX=2, 接IN口)  |  顶部14P / 其它接口: 空闲", 
                 "机身侧面接口驱动 8x8 单色点阵副屏，系统通过 WiFi NTP 对时或断网坐标显示过境动画。"),
                
                ("方案 8: 纯单机模拟演示版 (无外接硬件)", 
                 "无需连接任何外部硬件模块，依靠 Cardputer 单机脱机运行", 
                 "开机零硬件门槛，系统通过 WiFi NTP 授时或直接按 C 键十字准星手动输入任意观测点经纬度。"),
            ]
        }
    ]

    card_y = 430
    for sc in sc_cards:
        c_h = 328
        draw_rounded_rect(draw, [40, card_y, W - 40, card_y + c_h], radius=6, fill=CARD_BG, outline=CARD_BORDER, width=1)
        
        # 顶栏
        draw_rounded_rect(draw, [40, card_y, W - 40, card_y + 36], radius=6, fill=CARD_HEADER_BG)
        draw.rectangle([40, card_y + 26, W - 40, card_y + 36], fill=CARD_HEADER_BG)
        draw.text((56, card_y + 8), sc["group"], fill=TEXT_MAIN, font=card_title_font)
        draw_badge(draw, W - 140, card_y + 6, sc["badge"], tag_font, "#212836", "#8b949e", pad_x=6, pad_y=2, radius=3, border_color="#30363d")
        
        # 副说明
        draw.text((56, card_y + 42), sc["sub"], fill=TEXT_MUTED, font=text_small)
        draw.line([(56, card_y + 60), (W - 56, card_y + 60)], fill=LINE_COLOR, width=1)
        
        # 4 个子项
        iy = card_y + 70
        for title, wiring, desc in sc["items"]:
            # 方案名
            draw.text((56, iy), title, fill="#58a6ff", font=text_bold)
            # 接口接线
            draw.text((450, iy), "→  " + wiring, fill="#f0f6fc", font=text_normal)
            # 说明
            draw.text((56, iy + 20), desc, fill=TEXT_MUTED, font=text_small)
            iy += 60
            if iy < card_y + c_h - 10:
                draw.line([(56, iy - 7), (W - 56, iy - 7)], fill="#1a202c", width=1)

        card_y += c_h + 16

    # 4. 第三大区：浑仪三轴机械动作与舵机映射 (Gimbal Architecture)
    draw.text((40, 1124), "三、浑仪三轴机械动作与舵机通道映射 (Orbital Arch Gimbal Motion Mapping)", fill=TEXT_ACCENT, font=section_font)

    gimbal_y = 1156
    gimbal_h = 176
    draw_rounded_rect(draw, [40, gimbal_y, W - 40, gimbal_y + gimbal_h], radius=6, fill=CARD_BG, outline=CARD_BORDER, width=1)

    # 顶栏
    draw_rounded_rect(draw, [40, gimbal_y, W - 40, gimbal_y + 34], radius=6, fill=CARD_HEADER_BG)
    draw.rectangle([40, gimbal_y + 24, W - 40, gimbal_y + 34], fill=CARD_HEADER_BG)
    draw.text((56, gimbal_y + 8), "三轴立体拱门联动机构 (CH0 / CH1 / CH2 舵机通道规范)", fill=TEXT_MAIN, font=card_title_font)
    draw_badge(draw, W - 150, gimbal_y + 6, "Unit 8Servos (0x25)", tag_font, "#212836", "#8b949e", pad_x=6, pad_y=2, radius=3, border_color="#30363d")

    gimbal_cols = [
        ("CH0: 底座方位走向长梁轴", "0° ~ 180°", [
            "物理结构: 旋转浑仪底座白色水平大梁",
            "动作映射: 对齐卫星本次过境在地面投影的整体航向基准线 (Track Heading)",
            "调头算法: 卫星向西飞行时，系统算法自动翻转 180° 补偿几何对称性"
        ]),
        ("CH1: 天球拱门倾角仰角轴", "30° ~ 180°", [
            "物理结构: 控制黑色天球拱门从地平线上仰起",
            "动作映射: 拱门最高顶点精确对应卫星过境的最高仰角 MaxEl",
            "安全保护: 单侧受机械大梁阻挡，系统内置 30°~180° 防卡死软件限位"
        ]),
        ("CH2: 拱顶星位滑行指针轴", "0° ~ 180°", [
            "物理结构: 位于拱门顶部的星位飞行指示指针",
            "动作映射: 代表卫星本体，随过境时间进度从 AOS 升起、滑过天顶、落入 LOS",
            "平滑插值: 实时接收 SGP4 绝对过境进度，重现太空航行立体弧线"
        ]),
    ]

    gw = (W - 80 - 24) // 3
    for j, (g_title, g_range, g_lines) in enumerate(gimbal_cols):
        gx = 52 + j * (gw + 12)
        gy = gimbal_y + 44
        draw_rounded_rect(draw, [gx, gy, gx + gw, gy + 118], radius=5, fill="#121720", outline="#252f3e", width=1)
        draw.text((gx + 12, gy + 8), g_title, fill=TEXT_MAIN, font=text_bold)
        draw_badge(draw, gx + gw - 66, gy + 6, g_range, tag_font, "#1f2937", "#9ca3af", pad_x=4, pad_y=2, radius=3, border_color="#374151")
        
        ly = gy + 34
        for l in g_lines:
            draw.text((gx + 12, ly), l, fill=TEXT_MUTED, font=text_small)
            ly += 23

    # 5. 第四大区：电气规范与安装注意事项 (Electrical Rules & Tips)
    rule_y = 1346
    draw_rounded_rect(draw, [40, rule_y, W - 40, rule_y + 90], radius=6, fill="#141923", outline="#28303f", width=1)
    
    draw.text((56, rule_y + 12), "电气安装与硬件规范注意事项 (Precautions):", fill="#e3b341", font=text_bold)
    rules = [
        ("• 硬件配置向导:", "在任意主界面随时按 M 键可呼出硬件向导，自由开关外设并由系统自动校验总线冲突与显示动态接线指引。"),
        ("• 舵机独立供电:", "三轴浑仪乐高舵机工作峰值电流可达 1.5A，Unit 8Servos 绿色接线端子必须接入 5V DC 独立电源，严禁仅依靠主控带载。"),
        ("• 副屏插接端口:", "Chain Mono 模块自带 IN 与 OUT 两个 Grove 接口，连接线缆必须插接至 IN 端口（插 OUT 端口将无法通信）。"),
        ("• 侧面端口独占:", "机身侧面 Grove 接口为单通道，无 Cap 时不可通过无源分线器同时连接多个外设。"),
    ]
    
    ry = rule_y + 36
    for i, (rtitle, rdesc) in enumerate(rules):
        col_x = 56 if i % 2 == 0 else 900
        row_y = ry if i < 2 else ry + 24
        draw.text((col_x, row_y), rtitle, fill="#f0f6fc", font=text_small)
        draw.text((col_x + 95, row_y), rdesc, fill=TEXT_MUTED, font=text_small)

    # 6. 底部 Footer
    draw.rectangle([0, H - 36, W, H], fill="#0b0f17")
    draw.line([(0, H - 36), (W, H - 36)], fill=LINE_COLOR, width=1)
    draw.text((40, H - 24), "SkyCompass Satellite Project | 硬件外设支持用户按需自由组合，系统自适应总线调度", fill="#6e7681", font=text_small)
    draw.text((W - 370, H - 24), "Docs: docs/schematic_diagram.png | scripts/generate_schematic.py", fill="#6e7681", font=text_small)

    # 保存输出
    output_path = "docs/schematic_diagram.png"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, "PNG", quality=95)
    print(f"Schematic diagram successfully regenerated at: {output_path} ({W}x{H})")

if __name__ == "__main__":
    generate_schematic()
