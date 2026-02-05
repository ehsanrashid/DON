#!/usr/bin/env python3
import json
import pathlib

# Root folder of your repo
repoRoot = pathlib.Path('.github')

# Recursively find all JSON files
for jsonFile in repoRoot.rglob('*.json'):
    try:
        # Load JSON
        with open(jsonFile, 'r', encoding='utf-8') as f:
            data = json.load(f)
        # Write JSON with 2-space indentation
        with open(jsonFile, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write('\n')  # final newline
        print(f'Formatted: {jsonFile}')
    except Exception as e:
        print(f'Skipping {jsonFile}: {e}')
