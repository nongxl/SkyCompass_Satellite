import urllib.request
import ssl
import os
import math
import random

# Ensure Pillow is installed (we already ran pip install pillow, but let's double check imports)
try:
    from PIL import Image
except ImportError:
    print("Pillow not found, installing via pip...")
    import subprocess
    import sys
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow"])
    from PIL import Image

def download_tiles():
    # NASA GIBS VIIRS Black Marble Z=2 tiles (16 tiles in total, x:0..3, y:0..3)
    tile_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiles_cache")
    if not os.path.exists(tile_dir):
        os.makedirs(tile_dir)
        
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    print("Downloading 16 tiles from NASA GIBS...")
    tiles = {}
    for y in range(4):
        for x in range(4):
            url = f"https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/default/GoogleMapsCompatible_Level8/2/{y}/{x}.png"
            tile_path = os.path.join(tile_dir, f"tile_2_{y}_{x}.png")
            
            # Use cached tile if exists to save bandwidth
            if not os.path.exists(tile_path):
                try:
                    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                    with urllib.request.urlopen(req, context=ctx) as r:
                        with open(tile_path, "wb") as f:
                            f.write(r.read())
                    # print(f" Downloaded tile ({x}, {y})")
                except Exception as e:
                    print(f"Error downloading tile ({x}, {y}) from {url}: {e}")
                    raise e
            tiles[(x, y)] = tile_path
    print("All 16 tiles are downloaded/ready.")
    return tiles

def assemble_and_sample(tiles):
    # Assemble 16 tiles into a 1024x1024 master image
    print("Assembling master image...")
    master = Image.new("RGB", (1024, 1024))
    for y in range(4):
        for x in range(4):
            tile_img = Image.open(tiles[(x, y)])
            master.paste(tile_img, (x * 256, y * 256))
    
    print("Sampling grid...")
    # Setup Equirectangular Grid (360x180)
    # longitude: -180 to 180, latitude: -90 to 90
    W, H = 360, 180
    grid = []
    
    # Pre-load master pixel data for fast access
    pixels = master.load()
    
    for gy in range(H):
        # gy=0 is north, gy=179 is south
        lat = 90.0 - (gy + 0.5) * (180.0 / H)
        for gx in range(W):
            lon = -180.0 + (gx + 0.5) * (360.0 / W)
            
            # Map Lat/Lon WGS84 to Mercator pixel coords on 1024x1024 image
            lat_rad = math.radians(lat)
            # Clip lat_rad to avoid tan(90 deg) or infinity
            lat_rad = max(-1.48, min(1.48, lat_rad))
            
            # Mercator Y formula
            y_merc = 0.5 - math.log(math.tan(math.pi / 4.0 + lat_rad / 2.0)) / (2.0 * math.pi)
            
            # Pixel coords
            px = int(((lon + 180.0) / 360.0) * 1024.0)
            py = int(y_merc * 1024.0)
            
            px = max(0, min(1023, px))
            py = max(0, min(1023, py))
            
            # Get pixel brightness (RGB -> Gray)
            r, g, b = pixels[px, py]
            brightness = int(0.299 * r + 0.587 * g + 0.114 * b)
            
            # Add to grid if has light pollution
            # Exclude polar regions (South of 60S, North of 75N) to filter out ice sheet reflections and edge logo noise
            if lat >= -60.0 and lat <= 75.0 and brightness > 15:
                grid.append({
                    'lat': lat,
                    'lon': lon,
                    'brightness': brightness
                })
                
    print(f"Found {len(grid)} lit grid sectors.")
    
    # Perform Importance Sampling
    random.seed(42) # Set seed for reproducible builds
    
    total_weights = sum(g['brightness'] for g in grid)
    if not grid:
        print("Warning: No light pollution detected in source tiles!")
        return []
        
    weights = [g['brightness'] for g in grid]
    
    # Sample 2000 points based on brightness weights to make the city lights denser
    target_count = 2000
    sampled_choices = random.choices(grid, weights=weights, k=target_count)
    
    sampled_points = []
    for s in sampled_choices:
        # Add a small random jitter within the 1-degree grid cell to look natural
        lat_jitter = s['lat'] + random.uniform(-0.4, 0.4)
        lon_jitter = s['lon'] + random.uniform(-0.4, 0.4)
        
        # Clip lat
        lat_jitter = max(-89.9, min(89.9, lat_jitter))
        # Wrap lon
        if lon_jitter > 180.0: lon_jitter -= 360.0
        elif lon_jitter < -180.0: lon_jitter += 360.0
        
        sampled_points.append((lat_jitter, lon_jitter))
        
    return sampled_points

def write_header(points):
    header_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 
        "src", "core", "light_points_data.h"
    )
    
    cpp_code = "#ifndef LIGHT_POINTS_DATA_H\n#define LIGHT_POINTS_DATA_H\n\n"
    cpp_code += "struct LightPoint {\n"
    cpp_code += "    float latRad;\n"
    cpp_code += "    float lonRad;\n"
    cpp_code += "    float sinLat;\n"
    cpp_code += "    float cosLat;\n"
    cpp_code += "};\n\n"
    cpp_code += f"const LightPoint light_points[] = {{\n"
    
    lines = []
    for p in points:
        lat_rad = math.radians(p[0])
        lon_rad = math.radians(p[1])
        sin_lat = math.sin(lat_rad)
        cos_lat = math.cos(lat_rad)
        lines.append(f"    {{{lat_rad:.6f}f, {lon_rad:.6f}f, {sin_lat:.6f}f, {cos_lat:.6f}f}}")
    
    cpp_code += ",\n".join(lines)
    cpp_code += f"\n}};\n\nconst int light_points_count = {len(points)};\n\n#endif\n"
    
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(cpp_code)
    print(f"Successfully generated: {header_path} with {len(points)} points.")

if __name__ == "__main__":
    try:
        tiles = download_tiles()
        points = assemble_and_sample(tiles)
        write_header(points)
    except Exception as e:
        print(f"Execution failed: {e}")
