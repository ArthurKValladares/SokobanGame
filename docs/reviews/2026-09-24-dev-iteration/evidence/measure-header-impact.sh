#!/bin/bash
# Counts what a Ninja build would redo after touching each given file.
#
# Usage (from the repository root, after a complete Ninja build in $BUILD):
#   python3 - "$BUILD" <<'EOF'
#   import sys; b=sys.argv[1]; out=[]; skip=False
#   for ln in open(f'{b}/build.ninja').read().split('\n'):
#       if ln.startswith(('build build.ninja', 'build CMakeFiles/VerifyGlobs',
#                         'build CMakeFiles/cmake.verify_globs')) or \
#          ('/CMakeFiles/cmake.verify_globs' in ln and ln.startswith('build ')) or \
#          ('/CMakeFiles/VerifyGlobs' in ln and ln.startswith('build ')):
#           skip=True; continue
#       if skip and ln.startswith('  '): continue
#       skip=False; out.append(ln)
#   open(f'{b}/noregen.ninja','w').write('\n'.join(out))
#   EOF
#   evidence/measure-header-impact.sh "$BUILD" src/engine/Math.hpp ...
#
# The noregen copy is needed because CONFIGURE_DEPENDS globbing makes every
# dry run stop at "Re-running CMake" instead of printing the real plan. Each
# file's mtime is restored afterwards, so the build tree stays up to date.
B=$1; shift
for f in "$@"; do
  cp -p "$f" /tmp/impact-ref.tmp
  touch "$f"
  out=$(ninja -C "$B" -f noregen.ninja -n 2>/dev/null)
  c=$(echo "$out" | grep -c 'Building CXX')
  l=$(echo "$out" | grep -c 'Linking CXX executable')
  a=$(echo "$out" | grep -c 'Linking CXX static')
  printf "%-50s compile=%3d exe_links=%3d lib_archives=%d\n" "$f" "$c" "$l" "$a"
  touch -r /tmp/impact-ref.tmp "$f"
done
