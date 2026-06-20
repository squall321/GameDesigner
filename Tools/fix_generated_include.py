#!/usr/bin/env python3
"""
Reorder a reflected UE header so its "<Name>.generated.h" include is the LAST #include in the file.

UnrealHeaderTool requires the generated header to be the final include (any #include after it is a
hard error). Area-generated headers sometimes place a version-gated FInstancedStruct block after the
.generated.h. This tool moves the .generated.h line to sit immediately after the last *other* #include
(including the closing #endif of a trailing __has_include block), preserving all other content.

Usage: python fix_generated_include.py <file.h> [<file.h> ...]
Idempotent: a file already correct is left unchanged.
"""
import re
import sys


def fix(path):
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    gen_idx = None
    gen_line = None
    for i, ln in enumerate(lines):
        if re.match(r'\s*#\s*include\s+"[^"]+\.generated\.h"', ln):
            gen_idx = i
            gen_line = ln
            break
    if gen_idx is None:
        return False  # no generated include — nothing to do

    # Find the index of the last "real" line that must precede .generated.h: the last #include that
    # is NOT the generated one, or the last #endif belonging to a preprocessor include block.
    last_real = -1
    for i, ln in enumerate(lines):
        if i == gen_idx:
            continue
        s = ln.strip()
        if re.match(r'#\s*include\s+"', s) or s.startswith("#if") or s.startswith("#elif") \
                or s.startswith("#else") or s.startswith("#endif"):
            last_real = i

    if last_real <= gen_idx:
        # generated.h already after the last include block — but double-check it's truly last include.
        # If there is any #include after gen_idx, we still need to move it.
        later_include = any(
            re.match(r'\s*#\s*include\s+"', lines[j]) for j in range(gen_idx + 1, len(lines)))
        if not later_include:
            return False  # already correct

    # Remove the generated line, then re-insert right after `last_real` (recompute after removal).
    del lines[gen_idx]
    if last_real > gen_idx:
        last_real -= 1  # account for the removed line shifting indices

    insert_at = last_real + 1
    block = []
    if insert_at <= len(lines) and (insert_at == 0 or lines[insert_at - 1].strip() != ""):
        block.append("\n")
    block.append("// .generated.h MUST be the last include (UnrealHeaderTool requirement).\n")
    block.append(gen_line if gen_line.endswith("\n") else gen_line + "\n")

    lines[insert_at:insert_at] = block

    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.writelines(lines)
    return True


def main():
    changed = 0
    for path in sys.argv[1:]:
        try:
            if fix(path):
                print(f"FIXED  {path}")
                changed += 1
            else:
                print(f"ok     {path}")
        except Exception as e:  # noqa
            print(f"ERROR  {path}: {e}")
    print(f"--- {changed} file(s) reordered ---")
    return 0


if __name__ == "__main__":
    sys.exit(main())
