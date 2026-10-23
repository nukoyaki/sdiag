#!/usr/bin/env python3

from PIL import Image

def generate_tight_font():
    img = Image.open("font.png").convert("1")

    char_width = 10
    char_height = 19
    start_char = ord('!')
    end_char = ord('~')
    num_chars = (end_char - start_char) + 1

    data = bytearray()

    for i in range(num_chars):
        y_offset = i * char_height
        char_bits = []

        for y in range(char_height):
            for x in range(char_width):
                pixel = img.getpixel((x, y_offset + y))
                bit = 1 if pixel > 127 else 0
                char_bits.append(bit)

        current_byte = 0
        bit_count = 0
        for bit in char_bits:
            current_byte = (current_byte << 1) | bit
            bit_count += 1
            if bit_count == 8:
                data.append(current_byte)
                current_byte = 0
                bit_count = 0

        if bit_count > 0:
            current_byte <<= (8 - bit_count)
            data.append(current_byte)

    with open("font.raw", "wb") as f:
        f.write(data)

if __name__ == "__main__":
    generate_tight_font()