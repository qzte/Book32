
import os
from PIL import Image
import re

# Configuration
ICON_MAP = {
    'icon_reader.png': {
        'path': r'c:\Users\Ron\Downloads\DEV\Book32-TRMNL\Book32\lib\Apps\AppReader\icon_reader.h',
        'var_name': 'icon_reader_80x80'
    },
    'icon_klipper.png': {
        'path': r'c:\Users\Ron\Downloads\DEV\Book32-TRMNL\Book32\lib\Apps\AppKlipper\icon_klipper.h',
        'var_name': 'icon_klipper_80x80'
    },
    'icon_todo.png': {
        'path': r'c:\Users\Ron\Downloads\DEV\Book32-TRMNL\Book32\lib\Apps\AppTodo\icon_todo.h',
        'var_name': 'icon_todo_80x80'
    }
}

ARTIFACT_DIR = r'c:\Users\Ron\.gemini\antigravity\brain\a13391f3-3a77-4921-9eee-f350113d7a8a'

def process_image(image_path):
    img = Image.open(image_path).convert('1') # Convert to 1-bit monochrome
    img = img.resize((80, 80))
    
    width, height = img.size
    hex_data = []
    
    # Process line by line
    for y in range(height):
        # We need to pack 80 pixels into 10 bytes (8 pixels per byte)
        row_bytes = []
        current_byte = 0
        for x in range(width):
            pixel = img.getpixel((x, y))
            # If pixel is black (0), set the bit. If white (255), clear the bit.
            # The existing code seemingly uses 0 for white/transparent and 1 for black?
            # Let's check the existing file content logic.
            # In typical monochrome LCDs/E-ink:
            # Often 1 = Black, 0 = White.
            # Let's assume standard MSB first packing.
            
            bit_pos = 7 - (x % 8)
            if pixel == 0: # Black
                current_byte |= (1 << bit_pos)
            
            if (x + 1) % 8 == 0:
                row_bytes.append(f"0x{current_byte:02x}")
                current_byte = 0
        
        hex_data.append(", ".join(row_bytes))
        
    return hex_data

def update_header_file(file_path, var_name, hex_data):
    with open(file_path, 'r') as f:
        content = f.read()

    # Create the new array content string
    new_array_content = "PROGMEM = {\n"
    for row in hex_data:
        new_array_content += "    " + row + ",\n"
    new_array_content += "};"
    
    # Regex to replace the array body
    # Match generic "const unsigned char SOMETHING[] PROGMEM = { ... };"
    pattern = re.compile(r'const unsigned char .*?\[\] PROGMEM = \{.*?\};', re.DOTALL)
    
    # Construct the full replacement string
    replacement = f'const unsigned char {var_name}[] {new_array_content}'
    
    new_content = pattern.sub(replacement, content)
    
    with open(file_path, 'w') as f:
        f.write(new_content)
    print(f"Updated {file_path}")

def main():
    # Find the actual filenames in the artifact dir (they have timestamps)
    files = os.listdir(ARTIFACT_DIR)
    
    for key, output_info in ICON_MAP.items():
        # Find the matching file (e.g. icon_reader_12345.png matches icon_reader)
        # key is like 'icon_reader.png'
        prefix = key.replace('.png', '')
        
        matching_file = None
        for f in files:
            if f.startswith(prefix) and f.endswith('.png'):
                # Pick the latest one if multiple? Usually just one.
                matching_file = os.path.join(ARTIFACT_DIR, f)
                break
        
        if matching_file:
            print(f"Processing {matching_file} for {key}")
            hex_data = process_image(matching_file)
            update_header_file(output_info['path'], output_info['var_name'], hex_data)
        else:
            print(f"Could not find image for {key}")

if __name__ == '__main__':
    main()
