from PIL import Image
import sys

def convert_icon(image_path, output_path, var_name):
    img = Image.open(image_path).convert('1') # Convert to 1-bit monochrome
    width, height = img.size
    
    if width != 160:
        print(f"Warning: Width is {width}, resizing to 160x160")
        img = img.resize((160, 160))
        width, height = 160, 160
        
    pixels = list(img.getdata())
    
    # Pack bits
    bytes_list = []
    current_byte = 0
    bit_count = 0
    
    # GFX Bitmap format: Row by row, MSB first
    # 1 = Color (Black), 0 = Bg (White)
    # The image has 0=Black, 255=White usually after convert('1')?
    # Let's check. In PIL '1' mode: 0 is black, 255 is white (normally).
    # But we want 1=Black for GFX drawBitmap.
    # So if pixel is 0 (Black), set bit to 1. If pixel > 0 (White), set bit to 0.
    
    for y in range(height):
        for x in range(width):
            pixel = img.getpixel((x, y))
            # PIL '1' mode: 0=black, 255=white (or 1=white depending on dither?)
            # Usually 0=Black.
            # We want 1=Black.
            bg_pixel = 255 if pixel > 0 else 0
            
            bit = 1 if bg_pixel == 0 else 0
            
            current_byte = (current_byte << 1) | bit
            bit_count += 1
            
            if bit_count == 8:
                bytes_list.append(current_byte)
                current_byte = 0
                bit_count = 0
                
        # Padding for row end? GFX usually doesn't pad rows to byte boundary if width%8 != 0?
        # Actually GFX treats it as a continuous stream but usually we align rows.
        # But 80 IS divisible by 8. So no padding needed.
        
    line_len = width // 8
    
    with open(output_path, 'w') as f:
        f.write(f"#ifndef {var_name.upper()}_H\n")
        f.write(f"#define {var_name.upper()}_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"const unsigned char {var_name}[] PROGMEM = {{\n")
        
        for i, b in enumerate(bytes_list):
            f.write(f"0x{b:02x}, ")
            if (i + 1) % line_len == 0:
                f.write("\n")
                
        f.write("};\n\n")
        f.write(f"#endif // {var_name.upper()}_H\n")

if __name__ == "__main__":
    convert_icon("icon_reader.png", "lib/Apps/AppReader/icon_reader.h", "icon_reader_160x160")
    convert_icon("icon_klipper.png", "lib/Apps/AppKlipper/icon_klipper.h", "icon_klipper_160x160")
    convert_icon("icon_todo.png", "lib/Apps/AppTodo/icon_todo.h", "icon_todo_160x160")
    convert_icon("icon_update.png", "lib/Book32_Apps/icon_update.h", "icon_update_160x160")
