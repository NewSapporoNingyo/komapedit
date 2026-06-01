from PIL import Image

input_file = "komapedit_logo.png"
output_file = "komapedit_logo.ico"

sizes = [
    (16, 16),
    (24, 24),
    (32, 32),
    (48, 48),
    (64, 64),
    (128, 128),
    (256, 256),
]

img = Image.open(input_file).convert("RGBA")
img.save(output_file, format="ICO", sizes=sizes)

print("ICO file created:", output_file)