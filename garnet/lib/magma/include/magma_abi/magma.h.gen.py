#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import textwrap
import sys

def usage():
  print 'Usage:'
  print '  magma.h.gen.py INPUT OUTPUT'
  print '    INPUT    json file containing the magma interface definition'
  print '    OUTPUT   destination path for the magma header file to generate'
  print '  Example: ./magma.h.gen.py ./magma.json ./magma.h'
  print '  Generates the magma header based on a provided json definition.'
  print '  Description fields are generated in the Doxygen format.'

# Generate a string for a comment with a Doxygen tag, wrapping text as necessary.
def format_comment(type, comment):
  wrap = 100
  prefix_first = '/// \\' + type + ' '
  prefix_len = len(prefix_first)
  prefix_rest = '///'.ljust(prefix_len)
  lines = textwrap.wrap(comment, wrap - prefix_len)
  formatted = prefix_first + lines[0] + '\n'
  for line in lines[1:]:
    formatted += prefix_rest + line + '\n'
  return formatted

# Generate a string for a magma export object, including its comment block.
def format_export(export):
  signature = export['type'] + ' ' + export['name'] + '(\n'
  comment = '///\n' + format_comment('brief', export['description'])
  for argument in export['arguments']:
    signature += '    ' + argument['type'] + ' ' + argument['name'] + ',\n'
    comment += format_comment('param', argument['name'] + ' ' + argument['description'])
  signature = signature[:-2] + ');\n'
  comment += '///\n'
  return comment + signature

# License string for the top of the file.
def license():
  ret = ''
  ret += '// Copyright 2016 The Fuchsia Authors. All rights reserved.\n'
  ret += '// Use of this source code is governed by a BSD-style license that can be\n'
  ret += '// found in the LICENSE file.\n'
  return ret

# Guard macro that goes at the beginning/end of the header (after license).
def guards(begin):
  macro = 'GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_'
  if begin:
    return '#ifndef ' + macro + '\n#define ' + macro + '\n'
  return '#endif // ' + macro

# Begin/end C linkage scope.
def externs(begin):
  if begin:
    return '#if defined(__cplusplus)\nextern "C" {\n#endif\n'
  return '#if defined(__cplusplus)\n}\n#endif\n'

# Includes list.
def includes():
  ret = ''
  ret += '#include "magma_common_defs.h"\n'
  ret += '#include <stdint.h>\n'
  return ret

# Warning comment about auto-generation.
def genwarning():
  ret = '// NOTE: DO NOT EDIT THIS FILE!\n'
  ret += '// It is automatically generated by //garnet/lib/magma/include/magma_abi/magma.h.gen.py\n'
  return ret

def main():
  if (len(sys.argv) != 3):
    usage()
    exit(-1)
  try:
    with open(sys.argv[1], 'r') as file:
      with open(sys.argv[2], 'w') as dest:
        magma = json.load(file)['magma-interface']
        header = license() + '\n'
        header += genwarning() + '\n'
        header += guards(True) + '\n'
        header += includes() + '\n'
        header += externs(True) + '\n'
        for export in magma['exports']:
          header += format_export(export) + '\n'
        header += externs(False) + '\n'
        header += guards(False) + '\n'
        dest.write(header)
  except:
    print 'Error accessing files.'
    usage()
    exit(-2)

if __name__ == '__main__':
  sys.exit(main())
