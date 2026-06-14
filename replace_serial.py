import os
import re

def replace_in_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Simple replaces for strings
    content = re.sub(r'Serial\.println\("([^"%]+)"\);', r'log_i("\1");', content)
    content = re.sub(r'Serial\.printf\((.*?)\);', r'log_i(\1);', content)
    
    # Specific ones
    content = content.replace('Serial.println("No WiFi credentials available. Falling back to WiFi Setup.");', 'log_i("No WiFi credentials available. Falling back to WiFi Setup.");')
    content = content.replace('Serial.println("WiFi connection failed. Falling back to WiFi Setup.");', 'log_i("WiFi connection failed. Falling back to WiFi Setup.");')
    content = content.replace('Serial.println("Network tasks complete. Turning off WiFi to save power.");', 'log_i("Network tasks complete. Turning off WiFi to save power.");')
    content = content.replace('Serial.println("Network tasks complete. WiFi remains connected.");', 'log_i("Network tasks complete. WiFi remains connected.");')
    content = content.replace('Serial.println("TLE Data is ready and models updated!");', 'log_i("TLE Data is ready and models updated!");')
    
    # HalWifi
    content = content.replace('Serial.print("IP Address: ");\n        Serial.println(WiFi.localIP());', 'log_i("IP Address: %s", WiFi.localIP().toString().c_str());')
    content = content.replace('Serial.print("Current time: ");\n        Serial.println(asctime(&timeinfo));', 'log_i("Current time: %s", asctime(&timeinfo));')
    
    # Others
    content = re.sub(r'Serial\.println\(([^;]+)\);', r'log_i("%s", String(\1).c_str());', content)
    content = re.sub(r'Serial\.print\(([^;]+)\);', r'log_i("%s", String(\1).c_str());', content)

    # Clean up double stringing
    content = re.sub(r'log_i\("%s", String\("(.*?)"\)\.c_str\(\)\);', r'log_i("\1");', content)
    
    # F macros
    content = re.sub(r'log_i\("%s", String\(F\("(.*?)"\)\)\.c_str\(\)\);', r'log_i("\1");', content)

    # Avoid replacing Serial.begin
    # (Regexes didn't touch Serial.begin, but let's make sure Serial1 isn't touched mistakenly where it's a GNSS read)
    # The regex r'Serial\.print' doesn't match 'Serial1' because of the '.'. But wait, what if 'Serial1.println'?
    # The regex is `Serial\.println` which has a literal dot. So it only matches `Serial.println`.

    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

for root, _, files in os.walk('src'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.h'):
            replace_in_file(os.path.join(root, file))
