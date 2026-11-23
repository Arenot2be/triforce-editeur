#!/bin/bash
# dec.sh <bin> <data off hex> <ctl off hex> <out.glsl>
python3 -c "
d=open('$1','rb'); d.seek(int('$2',16)); open('${TMPDIR:-/tmp}/_c.code','wb').write(d.read(0x40000))
d.seek(int('$3',16)); open('${TMPDIR:-/tmp}/_c.ctl','wb').write(d.read(0x800))
"
${SHADERTOOL:-shadertool} ${TMPDIR:-/tmp}/_c.code ${TMPDIR:-/tmp}/_c.spv > ${TMPDIR:-/tmp}/_c.log 2>&1 || { tail -2 ${TMPDIR:-/tmp}/_c.log; exit 1; }
${SPIRV_CROSS:-spirv-cross} ${TMPDIR:-/tmp}/_c.spv --version 460 --vulkan-semantics 2>&1 | grep -v "_48(0u), _48(0u)" > $4
wc -l < $4
