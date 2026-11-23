#!/bin/bash
# opt.sh in.spv out.glsl : spirv-opt -O puis GLSL
${SPIRV_OPT:-spirv-opt} -O --skip-validation $1 -o ${TMPDIR:-/tmp}/_o.spv 2>${TMPDIR:-/tmp}/_o.log || { cat ${TMPDIR:-/tmp}/_o.log | head -3; cp $1 ${TMPDIR:-/tmp}/_o.spv; }
${SPIRV_CROSS:-spirv-cross} ${TMPDIR:-/tmp}/_o.spv --version 460 --vulkan-semantics 2>&1 | grep -v "_48(0u), _48(0u)" > $2
wc -l < $2
