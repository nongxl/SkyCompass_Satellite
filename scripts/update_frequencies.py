import json
import urllib.request
import os

# Define the path to the data folder
DATA_DIR = os.path.join(os.path.dirname(__file__), '..', 'data')
os.makedirs(DATA_DIR, exist_ok=True)
FREQ_FILE = os.path.join(DATA_DIR, 'frequencies.json')

# This is an example of what your script could do.
# In a real scenario, you could fetch from SatNOGS API:
# url = "https://db.satnogs.org/api/transmitters/?format=json"
# req = urllib.request.Request(url)
# with urllib.request.urlopen(req) as response:
#     data = json.loads(response.read().decode())
#
# Then filter for the NORAD IDs you care about.
# For demonstration, we just define a static dictionary that gets written to the JSON file.

sat_data = {
    "25544": {"freq": "145.800", "mode": "FM/SSTV"},
    "33591": {"freq": "137.100", "mode": "APT"},
    "4382": {"freq": "20.009", "mode": "Beacon"},
    "57165": {"freq": "137.100", "mode": "LRPT"}
}

# Example of an automated update: adding a new timestamp or updating a dynamic satellite
import time
sat_data["_metadata"] = {
    "last_updated": int(time.time()),
    "source": "GitHub Actions Auto Update"
}

with open(FREQ_FILE, 'w', encoding='utf-8') as f:
    json.dump(sat_data, f, indent=2)

print("Successfully updated frequencies.json")
