import urllib.request
import re
import ssl

def get_all_js_urls():
    url = "https://www.darkmap.cn/"
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    html = ""
    with urllib.request.urlopen(req, context=ctx) as response:
        html = response.read().decode('utf-8', errors='ignore')
        
    # Find all JS scripts (including ones in link tags and script tags)
    js_files = re.findall(r'href="(/_next/static/[^"]+\.js)"', html)
    js_files += re.findall(r'src="(/_next/static/[^"]+\.js)"', html)
    
    # Also parse manifest if it exists
    # Look for script sources in JSON configuration if any
    next_data = re.search(r'<script id="__NEXT_DATA__" type="application/json">([^<]+)</script>', html)
    if next_data:
        try:
            data = json.loads(next_data.group(1))
            # parse chunks from data
        except Exception:
            pass
            
    return list(set(js_files))

def search_content(chunks):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    
    # Let's search for any URL-like structures or config names
    keywords = ["lpm", "darkmap", "tile", "wa2015", "viirs", "bortle", "lightpollution"]
    
    for chunk in chunks:
        url = f"https://www.darkmap.cn{chunk}"
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, context=ctx) as r:
                content = r.read().decode('utf-8', errors='ignore')
                
                # Check for any of the keywords
                found_kws = [kw for kw in keywords if kw in content]
                if found_kws:
                    print(f"\n[!] Match in chunk: {chunk} (Keywords: {found_kws})")
                    
                    # Print matching sections
                    # Let's extract any substring that looks like a URL template containing lpm or tiles
                    urls = re.findall(r'https?://[^\'"\s>]+', content)
                    for u in urls:
                        if any(k in u for k in ["lpm", "darkmap", "tile"]):
                            print(f"  Found URL: {u}")
                            
                    # Let's print snippets around lpm.darkmap.cn (case-insensitive)
                    for kw in found_kws:
                        matches = re.finditer(re.escape(kw), content, re.IGNORECASE)
                        for m in list(matches)[:5]: # show max 5 snippets
                            start = max(0, m.start() - 60)
                            end = min(len(content), m.end() + 60)
                            snippet = content[start:end].replace('\n', ' ')
                            print(f"  Snippet ({kw}): ... {snippet} ...")
                            
        except Exception as e:
            # print(f"Failed to fetch {url}: {e}")
            pass

if __name__ == "__main__":
    print("Searching recursively for map tile config...")
    chunks = get_all_js_urls()
    print(f"Found {len(chunks)} JS chunks.")
    search_content(chunks)
