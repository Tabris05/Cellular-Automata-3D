# spv_to_c_array.py
import sys
import struct

def emit_c_array(input_path, output_path):
    with open(input_path, "rb") as f:
        data = f.read()

    with open(output_path, "w") as out:
        out.write("const uint32_t spirv_shader[] = {\n")
        for i, (word,) in enumerate(struct.iter_unpack("<I", data)):
            out.write(f"  0x{word:08x},")
            out.write("\n" if (i + 1) % 4 == 0 else " ")
        out.write("\n};\n")
        out.write(f"const size_t spirv_shader_size = {len(data) // 4};\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python spv_to_c_array.py input.spv output.h")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    emit_c_array(input_path, output_path)