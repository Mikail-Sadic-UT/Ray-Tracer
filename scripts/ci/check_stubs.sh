#!/usr/bin/env bash
# Checks the stub markers in code against the stub register in docs/ROADMAP.md.
#
#   marker:    // STUB(S3, jett): remove when AABB::empty() lands (#4)
#   register:  a table row in docs/ROADMAP.md that starts with "| S3 |"
#
# Prints every marker. Exits 1 if a marker is malformed (no ID or owner) or its ID has no
# register row. A register row with no marker is only noted, because a stub can live in a
# CMake file or be a one-line workaround. See docs/WORKFLOW.md "Stubs and local workarounds".
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# Docs and this script are excluded: they contain example markers.
markers=$(git grep --untracked -nE 'STUB\(' -- src apps tests scripts cmake CMakeLists.txt \
          ':!scripts/ci/check_stubs.sh' || true)
malformed=$(printf '%s\n' "$markers" | grep -vE 'STUB\(S[0-9]+, [a-z]+\)' | sed '/^$/d' || true)
code_ids=$(printf '%s\n' "$markers" | grep -oE 'STUB\(S[0-9]+' | grep -oE 'S[0-9]+' | sort -u || true)
reg_ids=$(grep -oE '^\| S[0-9]+ \|' docs/ROADMAP.md | grep -oE 'S[0-9]+' | sort -u || true)

status=0

if [ -n "$(printf '%s' "$markers")" ]; then
  echo "Stub markers in code:"
  printf '%s\n' "$markers" | sed 's/^/  /'
else
  echo "No stub markers in code."
fi

if [ -n "$malformed" ]; then
  echo "::error::Malformed STUB marker (expected: STUB(S<n>, <owner>): ...):"
  printf '%s\n' "$malformed" | sed 's/^/  /'
  status=1
fi

unregistered=$(comm -23 <(printf '%s\n' "$code_ids" | sed '/^$/d') \
                         <(printf '%s\n' "$reg_ids" | sed '/^$/d'))
if [ -n "$unregistered" ]; then
  echo "::error::STUB marker(s) with no row in the register (docs/ROADMAP.md): $(echo $unregistered)"
  status=1
fi

unmarked=$(comm -13 <(printf '%s\n' "$code_ids" | sed '/^$/d') \
                     <(printf '%s\n' "$reg_ids" | sed '/^$/d'))
if [ -n "$unmarked" ]; then
  echo "Note: register row(s) with no STUB marker in code: $(echo $unmarked)." \
       "Fine if the stub is in a non-code file; remove the row if the stub is gone."
fi

[ "$status" -eq 0 ] && echo "Stub register: OK"
exit "$status"
