"""Compile les shaders (glslangValidator) en SPIR-V et les range dans src/app/shaders_spv.hpp.
usage : python3 tools/gen_shaders.py <glslangValidator>"""
import os
import struct
import subprocess
import sys
import tempfile

root = os.path.join(os.path.dirname(__file__), "..")
glslang = sys.argv[1]
out = ["// Généré par tools/gen_shaders.py à partir de shaders/ : ne pas modifier à la main.", "#pragma once", "#include <cstdint>", ""]
for name in ["mesh.vert", "mesh.frag", "water.vert", "water.frag", "terrain.vert", "terrain.frag", "line.vert", "line.frag", "fx.vert", "fx.frag"]:
    with tempfile.TemporaryDirectory() as t:
        spv = os.path.join(t, "o.spv")
        subprocess.run([glslang, "-V", "--target-env", "vulkan1.1", "-o", spv, os.path.join(root, "shaders", name)], check=True,
                       stdout=subprocess.DEVNULL)
        data = open(spv, "rb").read()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    ident = "SPV_" + name.replace(".", "_").upper()
    out.append("static const uint32_t %s[] = {" % ident)
    for i in range(0, len(words), 8):
        out.append("    " + ", ".join("0x%08x" % w for w in words[i:i + 8]) + ",")
    out.append("};")
open(os.path.join(root, "src", "app", "shaders_spv.hpp"), "w").write("\n".join(out) + "\n")
print("ok")
