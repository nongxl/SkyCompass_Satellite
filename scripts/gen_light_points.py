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
    
    print("Sampling master pixels (high resolution)...")
    lit_pixels = []
    
    # Pre-load master pixel data for fast access
    pixels = master.load()
    
    # Scan every pixel in 1024x1024
    for py in range(1024):
        # Map Mercator Y coordinate to Lat (WGS84 radians)
        y_merc = py / 1024.0
        val = math.exp(2.0 * math.pi * (0.5 - y_merc))
        lat_rad = 2.0 * (math.atan(val) - math.pi / 4.0)
        lat = math.degrees(lat_rad)
        
        # Exclude polar regions to filter out ice sheet reflections and logo noise
        if lat < -60.0 or lat > 75.0:
            continue
            
        for px in range(1024):
            # Map Mercator X coordinate to Lon (WGS84 degrees)
            lon = -180.0 + (px / 1024.0) * 360.0
            
            r, g, b = pixels[px, py]
            brightness = int(0.299 * r + 0.587 * g + 0.114 * b)
            
            # Threshold filtering
            if brightness > 20:
                # Use moderate 1.1 power for weights to prevent megacities from starving interior capital cities
                weight = float(brightness) ** 1.1
                lit_pixels.append({
                    'lat': lat,
                    'lon': lon,
                    'weight': weight,
                    'brightness': brightness
                })
                
    print(f"Found {len(lit_pixels)} lit pixel candidates.")
    
    if not lit_pixels:
        print("Warning: No light pollution detected in source tiles!")
        return []
        
    random.seed(42) # Set seed for reproducible builds
    
    # Stratified Bins definition (Lower thresholds to capture medium-large inland capitals in higher tiers)
    tier1_cand = [p for p in lit_pixels if p['brightness'] >= 150]
    tier2_cand = [p for p in lit_pixels if 80 <= p['brightness'] < 150]
    tier3_cand = [p for p in lit_pixels if 40 <= p['brightness'] < 80]
    tier4_cand = [p for p in lit_pixels if 20 <= p['brightness'] < 40]
    
    print(f"Tiers - T1: {len(tier1_cand)}, T2: {len(tier2_cand)}, T3: {len(tier3_cand)}, T4: {len(tier4_cand)}")
    
    # Helper for weighted sampling without replacement
    def sample_without_replacement(candidates, k):
        if len(candidates) <= k:
            return candidates
        keys = [-math.log(random.random()) / p['weight'] for p in candidates]
        sorted_res = [x for _, x in sorted(zip(keys, candidates), key=lambda pair: pair[0])]
        return sorted_res[:k]
        
    # Step 1: Tier 1 (Ultra-high) -> All selected, 6 particles each
    s1 = sample_without_replacement(tier1_cand, len(tier1_cand))
    p1 = len(s1) * 6
    
    # Step 2: Tier 2 (High) -> Target 900 skeleton, 3 particles each
    s2 = sample_without_replacement(tier2_cand, 900)
    p2 = len(s2) * 3
    
    # Step 3: Tier 3 (Medium) -> Target 1200 skeleton, 2 particles each
    s3 = sample_without_replacement(tier3_cand, 1200)
    p3 = len(s3) * 2
    
    # Step 4: Tier 4 (Low) -> Calculate remaining spots, 1 particle each
    remaining = 9000 - (p1 + p2 + p3)
    s4 = sample_without_replacement(tier4_cand, remaining)
    
    print(f"Skeleton selected - T1: {len(s1)}, T2: {len(s2)}, T3: {len(s3)}, T4: {len(s4)}")
    
    sampled_points = []
    
    # Helper to append particles with adaptive jitter
    def append_particles(skeleton, fission_count, jitter):
        for s in skeleton:
            # 1. Real original point (always at center, zero drift)
            sampled_points.append((s['lat'], s['lon']))
            # 2. Split particles
            for _ in range(fission_count - 1):
                lat_jitter = s['lat'] + random.uniform(-jitter, jitter)
                lon_jitter = s['lon'] + random.uniform(-jitter, jitter)
                lat_jitter = max(-89.9, min(89.9, lat_jitter))
                if lon_jitter > 180.0: lon_jitter -= 360.0
                elif lon_jitter < -180.0: lon_jitter += 360.0
                sampled_points.append((lat_jitter, lon_jitter))
                
    append_particles(s1, 6, 1.00)   # Tier 1: 1.0 degree jitter (~1.0 pixel diffuse glow)
    append_particles(s2, 3, 0.45)   # Tier 2: 0.45 degree jitter (~0.45 pixel glow)
    append_particles(s3, 2, 0.22)   # Tier 3: 0.22 degree jitter (~0.2 pixel glow)
    append_particles(s4, 1, 0.0)    # Tier 4: No jitter (0.0 pixel, exact coordinates)
    
    # Pad if total count drifts slightly due to list sizing
    while len(sampled_points) < 9000 and s2:
        s = random.choice(s2)
        lat_jitter = s['lat'] + random.uniform(-0.3, 0.3)
        lon_jitter = s['lon'] + random.uniform(-0.3, 0.3)
        lat_jitter = max(-89.9, min(89.9, lat_jitter))
        if lon_jitter > 180.0: lon_jitter -= 360.0
        elif lon_jitter < -180.0: lon_jitter += 360.0
        sampled_points.append((lat_jitter, lon_jitter))
        
    unique_candidates = len(set((s['lat'], s['lon']) for s in s1 + s2 + s3 + s4))
    print(f"Sampled {len(sampled_points)} light pollution coordinates (from {unique_candidates} unique source skeleton pixels).")
    return sampled_points
 
def write_header(points):
    header_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 
        "src", "core", "light_points_data.h"
    )
    
    cpp_code = "#ifndef LIGHT_POINTS_DATA_H\n#define LIGHT_POINTS_DATA_H\n\n"
    cpp_code += "struct LightPoint {\n"
    cpp_code += "    float sinLat;\n"
    cpp_code += "    float cosLat;\n"
    cpp_code += "    float sinLon;\n"
    cpp_code += "    float cosLon;\n"
    cpp_code += "};\n\n"
    cpp_code += f"const LightPoint light_points[] = {{\n"
    
    lines = []
    for p in points:
        lat_rad = math.radians(p[0])
        lon_rad = math.radians(p[1])
        sin_lat = math.sin(lat_rad)
        cos_lat = math.cos(lat_rad)
        sin_lon = math.sin(lon_rad)
        cos_lon = math.cos(lon_rad)
        lines.append(f"    {{{sin_lat:.6f}f, {cos_lat:.6f}f, {sin_lon:.6f}f, {cos_lon:.6f}f}}")
    
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
