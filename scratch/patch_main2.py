import re

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    lines = f.read().splitlines()

# Fix 1: Remove `bool isSatViewMode = false;` from around line 206
for i in range(195, 215):
    if lines[i] == 'bool isSatViewMode = false;':
        lines[i] = '// removed duplicate isSatViewMode'

# Fix 2: Remove the extra `};` at line 782 and fix the block
for i in range(770, 795):
    if lines[i].strip() == '};' and "if (key == ' ')" in lines[i+1]:
        lines[i] = '                ' # Remove };

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')
