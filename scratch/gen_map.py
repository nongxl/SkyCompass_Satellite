import urllib.request
import json
import ssl
import os
import math

# Use 50m medium-resolution land polygons for enhanced coastlines
url = "https://cdn.jsdelivr.net/gh/nvkelso/natural-earth-vector@master/geojson/ne_50m_land.geojson"
print("Fetching 50m map data...")
try:
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, context=ctx) as response:
        data = json.loads(response.read().decode())
except Exception as e:
    print(f"Error fetching: {e}")
    exit(1)

polygons = []
for feature in data['features']:
    geom = feature['geometry']
    if geom['type'] == 'Polygon':
        polygons.append(geom['coordinates'][0])
    elif geom['type'] == 'MultiPolygon':
        for poly in geom['coordinates']:
            polygons.append(poly[0])

total_points = sum(len(p) for p in polygons)
print(f"Total points in raw 50m map: {total_points}")

cpp_code = "#ifndef EARTH_DATA_H\n#define EARTH_DATA_H\n\n"
cpp_code += "struct MapPoint {\n"
cpp_code += "    float sinLat;\n"
cpp_code += "    float cosLat;\n"
cpp_code += "    float sinLon;\n"
cpp_code += "    float cosLon;\n"
cpp_code += "    float latRad;\n"
cpp_code += "};\n\n"
cpp_code += "struct MapPath { int length; const MapPoint* points; };\n\n"

valid_polys = []
for i, poly in enumerate(polygons):
    # Filter out small islands (less than 30 source coordinates) to keep loop drawing lightweight
    if len(poly) < 30: continue 
    
    # Moderate downsampling: take every 6th point for 50m resolution (giving ~6000 total points)
    simplified = poly[::6]
    if simplified[-1] != poly[-1]:
        simplified.append(poly[-1])
        
    valid_polys.append((i, simplified))

total_valid_points = sum(len(p) for _, p in valid_polys)
print(f"Total points after downsampling: {total_valid_points}")

for i, poly in valid_polys:
    cpp_code += f"const MapPoint map_path_{i}[] = {{\n    "
    pts = []
    for pt in poly:
        lon, lat = pt
        lat_rad = math.radians(lat)
        lon_rad = math.radians(lon)
        sin_lat = math.sin(lat_rad)
        cos_lat = math.cos(lat_rad)
        sin_lon = math.sin(lon_rad)
        cos_lon = math.cos(lon_rad)
        pts.append(f"{{{sin_lat:.6f}f, {cos_lat:.6f}f, {sin_lon:.6f}f, {cos_lon:.6f}f, {lat_rad:.6f}f}}")
    cpp_code += ",\n    ".join(pts)
    cpp_code += "\n};\n\n"

cpp_code += f"const MapPath world_map[] = {{\n"
for i, poly in valid_polys:
    cpp_code += f"    {{{len(poly)}, map_path_{i}}},\n"
cpp_code += "};\n"
cpp_code += f"const int world_map_count = {len(valid_polys)};\n\n"
cpp_code += "#endif\n"

output_path = "d:/workspace/SkyCompass_Satellite/src/core/earth_data.h"
with open(output_path, "w", encoding="utf-8") as f:
    f.write(cpp_code)
print(f"earth_data.h generated successfully! Output: {output_path}")
