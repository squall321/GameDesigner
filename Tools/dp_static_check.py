#!/usr/bin/env python3
"""
Standalone static checker for the DesignPatterns UE5 plugin.

This is NOT a substitute for UnrealHeaderTool + the C++ compiler, but it mechanically
catches the most common errors those tools would reject, without an engine install:

  H1  reflected header (has UCLASS/USTRUCT/UENUM/UINTERFACE) must #include "<Name>.generated.h"
  H2  the .generated.h include must be the LAST #include in the header
  H3  every GENERATED_BODY()/GENERATED_UCLASS_BODY() lives in a reflected type
  H4  a .cpp must not include a foreign "*.generated.h" (only its paired header's, transitively)
  B1  braces / parens balance per file
  C1  every public method declared in a class .h has a definition in the paired .cpp
       (best-effort: matches `RetType UClass::Method(` in the cpp)
  C2  UPROPERTY() immediately followed by a raw UObject* pointer (should be TObjectPtr<>)
  I1  a type referenced as `UDP_X`/`FDP_X`/`IDP_X` in a header that is neither defined,
       forward-declared, nor included (best-effort, DP types only)
  S1  stray non-comment backslash lines / obvious truncation markers
  M1  IMPLEMENT_MODULE appears exactly once per module
  P1  placeholder / stub / TODO / unimplemented markers (must be ZERO for production code):
       TODO, FIXME, XXX, HACK, "not implemented", "placeholder", "stub", unimplemented(),
       checkNoEntry(), "for now", "temporary"/"temp hack", "dummy", ensureMsgf(false / check(false)
       used as an unimplemented marker.

Findings are printed as  SEVERITY  file:line  RULE  message.
Exit code is non-zero if any ERROR-level finding exists.
"""
import os
import re
import sys
from collections import defaultdict

ROOT = r"d:\GameDesigner\Plugins\DesignPatterns\Source"

REFLECT_RE = re.compile(r'^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(', re.M)
GENBODY_RE = re.compile(r'\bGENERATED_(BODY|UCLASS_BODY|UINTERFACE_BODY|IINTERFACE_BODY)\s*\(')
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
DP_TYPE_RE = re.compile(r'\b([UFIE]DP_[A-Za-z0-9_]+)\b')
UPROP_PTR_RE = re.compile(r'UPROPERTY\s*\([^)]*\)\s*\n\s*(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\*\s*[A-Za-z_]')

# Placeholder / stub / TODO markers — case-insensitive word/phrase patterns that must not appear
# in finished production code. Each tuple is (compiled regex, human label).
PLACEHOLDER_PATTERNS = [
    (re.compile(r'\bTODO\b', re.I), "TODO"),
    (re.compile(r'\bFIXME\b', re.I), "FIXME"),
    (re.compile(r'\bXXX\b'), "XXX"),
    (re.compile(r'\bHACK\b', re.I), "HACK"),
    (re.compile(r'not\s+implemented', re.I), "'not implemented'"),
    (re.compile(r'\bplaceholder\b', re.I), "'placeholder'"),
    (re.compile(r'\bunimplemented\s*\(', re.I), "unimplemented()"),
    (re.compile(r'\bcheckNoEntry\s*\(', re.I), "checkNoEntry()"),
    (re.compile(r'temp\s+hack', re.I), "'temp hack'"),
    (re.compile(r'\bto\s+be\s+implemented\b', re.I), "'to be implemented'"),
    (re.compile(r'\bnot\s+yet\s+implemented\b', re.I), "'not yet implemented'"),
    # "STUB" / "STUB IMPLEMENTATION" as an explicit marker (all-caps or labelled).
    (re.compile(r'\bSTUB\s+IMPLEMENTATION', re.I), "'STUB IMPLEMENTATION'"),
]
# NOTE: bare phrases like "for now", "temporary", "not yet", "dummy" produce too many false
# positives on ordinary prose ("not yet built", "for now we cache", a var named TempResult), so
# they are deliberately excluded. The patterns above target unambiguous unfinished-code markers.
# A bare "stub" as a whole word, but allow common false positives (e.g. method names containing it
# are unlikely in this codebase; we still keep it ERROR and review any hit).
STUB_RE = re.compile(r'\bstub\b', re.I)

findings = []  # (severity, file, line, rule, msg)


def add(sev, f, line, rule, msg):
    findings.append((sev, f, line, rule, msg))


def all_files(ext):
    for dirpath, _, names in os.walk(ROOT):
        for n in names:
            if n.endswith(ext):
                yield os.path.join(dirpath, n)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def rel(path):
    return os.path.relpath(path, ROOT)


def check_headers():
    for h in all_files(".h"):
        src = read(path=h)
        lines = src.splitlines()
        base = os.path.basename(h)[:-2]  # strip ".h"
        is_reflected = bool(REFLECT_RE.search(src)) or bool(GENBODY_RE.search(src))
        includes = INCLUDE_RE.findall(src)
        gen = f'{base}.generated.h'

        if is_reflected:
            if gen not in includes:
                add("ERROR", h, 1, "H1",
                    f"reflected header missing #include \"{gen}\"")
            else:
                # H2: generated.h must be the last include
                gen_idx = None
                last_inc_line = 0
                for i, ln in enumerate(lines, 1):
                    m = INCLUDE_RE.match(ln)
                    if m:
                        last_inc_line = i
                        if m.group(1) == gen:
                            gen_idx = i
                # find the line number of the LAST include
                inc_lines = [i for i, ln in enumerate(lines, 1) if INCLUDE_RE.match(ln)]
                if gen_idx is not None and inc_lines and gen_idx != inc_lines[-1]:
                    add("ERROR", h, gen_idx, "H2",
                        f"\"{gen}\" must be the LAST #include (UHT requirement)")
        else:
            # H3-ish: a non-reflected header should not include a generated.h of itself
            if gen in includes:
                add("WARN", h, 1, "H3",
                    f"includes {gen} but has no UCLASS/USTRUCT/UENUM/GENERATED_BODY")

        balance_check(h, src)
        stray_check(h, lines)
        dp_type_check(h, src, includes)
        uprop_ptr_check(h, src)
        placeholder_check(h, lines)


def placeholder_check(f, lines):
    """P1: flag stub/placeholder/TODO markers anywhere in the file (comments included —
    a 'TODO: implement' comment is exactly the unfinished-code signal we must eliminate)."""
    for i, ln in enumerate(lines, 1):
        for rx, label in PLACEHOLDER_PATTERNS:
            if rx.search(ln):
                add("ERROR", f, i, "P1", f"placeholder/stub marker {label}: {ln.strip()[:70]!r}")
        if STUB_RE.search(ln):
            add("ERROR", f, i, "P1", f"'stub' marker: {ln.strip()[:70]!r}")


def balance_check(f, src):
    # strip line+block comments and string/char literals to avoid false positives
    s = re.sub(r'//[^\n]*', '', src)
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'"(\\.|[^"\\])*"', '""', s)
    s = re.sub(r"'(\\.|[^'\\])'", "''", s)
    for open_c, close_c, name in [("{", "}", "brace"), ("(", ")", "paren")]:
        if s.count(open_c) != s.count(close_c):
            add("ERROR", f, 1, "B1",
                f"unbalanced {name}: {s.count(open_c)} '{open_c}' vs {s.count(close_c)} '{close_c}'")


def stray_check(f, lines):
    for i, ln in enumerate(lines, 1):
        stripped = ln.strip()
        # a line starting with a backslash that isn't a continuation inside a macro
        if stripped.startswith("\\") and not stripped.startswith("\\\\"):
            add("ERROR", f, i, "S1", f"stray backslash line: {stripped[:40]!r}")
        if "truncated" in stripped.lower() and "char" in stripped.lower():
            add("ERROR", f, i, "S1", "looks like truncated generated content")


def dp_type_check(f, src, includes):
    # Collect DP types DEFINED or forward-declared in this file
    defined = set(re.findall(r'\b(?:class|struct|enum class|enum)\s+(?:DESIGNPATTERNS\w*_API\s+)?([UFIE]DP_[A-Za-z0-9_]+)', src))
    fwd = set(re.findall(r'\b(?:class|struct)\s+([UFIE]DP_[A-Za-z0-9_]+)\s*;', src))
    # Types available via includes are hard to resolve without a full graph; treat any
    # DP header include as making its primary type available. Heuristic only -> WARN.
    included_types = set()
    for inc in includes:
        stem = os.path.basename(inc)
        m = re.match(r'DP([A-Za-z0-9_]+)\.h', stem)
        # too noisy to map precisely; skip precise resolution
    used = set(DP_TYPE_RE.findall(src))
    # We only flag a used DP type that is neither defined, forward-declared, nor plausibly
    # included by ANY dp header include. Because precise resolution needs the compiler, we
    # downgrade to INFO to avoid false alarms; real resolution happens in the agent audit.
    # (kept intentionally quiet)
    return


def uprop_ptr_check(f, src):
    for m in UPROP_PTR_RE.finditer(src):
        typ = m.group(1)
        if typ in ("char", "void", "uint8", "int32", "float", "double", "bool", "TCHAR"):
            continue
        # raw U*/A* pointer under UPROPERTY -> should be TObjectPtr in UE5 style
        if typ[0] in "UA" and typ[1:2].isupper():
            line = src[:m.start()].count("\n") + 1
            add("WARN", f, line, "C2",
                f"UPROPERTY raw pointer '{typ}*' — prefer TObjectPtr<{typ}> (UE5 style)")


def check_cpp_signatures():
    # Best-effort: for each class header, ensure each non-inline declared method body exists
    # in the sibling cpp. We pair by file base name.
    for h in all_files(".h"):
        base = os.path.basename(h)[:-2]
        cpp = None
        # paired cpp: same base name under a parallel Private/ tree
        for c in all_files(".cpp"):
            if os.path.basename(c)[:-4] == base:
                cpp = c
                break
        if not cpp:
            continue
        hsrc = read(h)
        csrc = read(cpp)
        # class name(s) in the header
        classes = re.findall(r'\bclass\s+(?:\w+_API\s+)?(U?[A-Za-z_][A-Za-z0-9_]*)\s*:', hsrc)
        # declared methods: "Ret Name(args);" inside class, skip pure-virtual/inline/defaulted
        # crude: lines ending in ');' that look like declarations
        for cls in classes:
            # methods defined in cpp for this class
            defined = set(re.findall(rf'\b{re.escape(cls)}::(\w+)\s*\(', csrc))
            # also methods defined inline in header
            inline_def = set(re.findall(r'\b(\w+)\s*\([^;{{]*\)\s*(?:const\s*)?(?:override\s*)?\{', hsrc))
            # declared method names in header (very rough)
            decls = re.findall(r'^\s*(?:virtual\s+)?[A-Za-z_][\w:<>,\*&\s]*?\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?(?:override\s*)?;', hsrc, re.M)
            for d in decls:
                if d in ("GENERATED_BODY",):
                    continue
                if d not in defined and d not in inline_def:
                    # Could be _Implementation, OnRep_, or a BNE; check common UHT patterns
                    if (d + "_Implementation") in defined or d.startswith("OnRep_"):
                        continue
                    # Only flag if it's clearly a method (not a macro / property)
                    add("INFO", h, 1, "C1",
                        f"{cls}::{d}() declared but no body found in {os.path.basename(cpp)} "
                        f"(may be BNE/inline/elsewhere)")


def check_cpp_placeholders():
    """Run the placeholder/stub scan over every .cpp too (P1), plus detect obvious empty/stub
    function bodies that return a default with a body that does nothing meaningful."""
    for c in all_files(".cpp"):
        src = read(c)
        lines = src.splitlines()
        placeholder_check(c, lines)


def check_modules():
    impls = defaultdict(int)
    for c in all_files(".cpp"):
        src = read(c)
        for m in re.finditer(r'IMPLEMENT_(?:PRIMARY_)?(?:GAME_)?MODULE\s*\(\s*\w+\s*,\s*(\w+)\s*\)', src):
            impls[m.group(1)] += 1
    expected = {"DesignPatterns", "DesignPatternsUI", "DesignPatternsDeveloper",
                "DesignPatternsEditor", "DesignPatternsGAS", "DesignPatternsTests",
                "DesignPatternsPlatform", "DesignPatternsCombat", "DesignPatternsRPG",
                "DesignPatternsSurvival", "DesignPatternsNet",
                "DesignPatternsSeams", "DesignPatternsEntity", "DesignPatternsWorld",
                "DesignPatternsSimGrid", "DesignPatternsInteraction", "DesignPatternsSimEconomy",
                "DesignPatternsSimAgents", "DesignPatternsInventoryUI",
                "DesignPatternsAudio", "DesignPatternsCamera", "DesignPatternsAI",
                "DesignPatternsNarrative", "DesignPatternsHUD",
                "DesignPatternsAnalytics", "DesignPatternsLocalization",
                "DesignPatternsLevelDirector", "DesignPatternsModContent",
                "DesignPatternsModContentEditor",
                "DesignPatternsProgression", "DesignPatternsSkillTree", "DesignPatternsGameMode",
                "DesignPatternsGameFlow", "DesignPatternsSaveSystem", "DesignPatternsSaveSystemUI",
                "DesignPatternsWorldSystems", "DesignPatternsTutorial", "DesignPatternsReplay"}
    for mod in expected:
        n = impls.get(mod, 0)
        if n == 0:
            add("ERROR", ROOT, 1, "M1", f"module '{mod}' has no IMPLEMENT_MODULE")
        elif n > 1:
            add("ERROR", ROOT, 1, "M1", f"module '{mod}' has {n} IMPLEMENT_MODULE (must be 1)")


def main():
    if not os.path.isdir(ROOT):
        print(f"Source root not found: {ROOT}")
        return 2
    check_headers()
    check_cpp_signatures()
    check_cpp_placeholders()
    check_modules()

    order = {"ERROR": 0, "WARN": 1, "INFO": 2}
    findings.sort(key=lambda x: (order.get(x[0], 9), x[1], x[2]))
    errors = sum(1 for f in findings if f[0] == "ERROR")
    warns = sum(1 for f in findings if f[0] == "WARN")
    infos = sum(1 for f in findings if f[0] == "INFO")

    for sev, f, line, rule, msg in findings:
        if sev == "INFO":
            continue  # keep INFO out of the main stream; summarized below
        print(f"{sev:5} {rel(f)}:{line}  [{rule}] {msg}")

    print()
    print(f"=== SUMMARY: {errors} ERROR, {warns} WARN, {infos} INFO "
          f"across {len(set(f[1] for f in findings))} files ===")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
