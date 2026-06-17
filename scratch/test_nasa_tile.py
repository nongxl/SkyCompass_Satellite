import urllib.request
import ssl
import os

def test_fetch():
    # Fetch a zoom level 1 tile from NASA GIBS
    url = "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/default/GoogleMapsCompatible_Level8/1/0/0.png"
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    print(f"Fetching NASA tile from: {url}")
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, context=ctx) as r:
            data = r.read()
            save_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nasa_tile_z1_0_0.png")
            with open(save_path, "wb") as f:
                f.write(data)
            print(f"Success! Image saved to: {save_path} (Size: {len(data)} bytes)")
    except Exception as e:
        print(f"Failed to fetch tile: {e}")

if __name__ == "__main__":
    test_fetch()
