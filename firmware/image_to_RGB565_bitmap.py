# Guardar los *.h generados en la misma carpeta que el sketch Arduino para usarlos en el código.

import argparse
from PIL import Image
import os

def convert_to_rgb565_header(image_path):
    try:
        # Abrir y redimensionar a la resolución de tu pantalla
        img = Image.open(image_path).convert("RGB")
        img = img.resize((280, 240), Image.Resampling.LANCZOS)
        
        width, height = img.size
        var_name = os.path.basename(image_path).split('.')[0].lower()
        output_file = f"{var_name}_img.h"

        with open(output_file, 'w') as f:
            f.write("#include <Arduino.h>\n\n")
            f.write(f"const uint16_t {var_name}_map[] PROGMEM = {{\n")
            
            pixels = []
            for y in range(height):
                for x in range(width):
                    r, g, b = img.getpixel((x, y))
                    # Conversión a RGB565
                    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    pixels.append(f"0x{rgb565:04X}")
                
                f.write(", ".join(pixels[-width:]) + ",\n")
                
            f.write("};\n")
            
        print(f"Éxito: {output_file} generado (280x240)")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('image_path', type=str)
    args = parser.parse_args()
    convert_to_rgb565_header(args.image_path)