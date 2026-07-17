#!/bin/bash
# check_docs.sh
# Local documentation health checks for docs/:
#   1. Relative markdown links resolve to existing files
#   2. Hand-written pages carry status/audience/last-verified frontmatter
#   3. Filenames follow kebab-case (with documented exceptions)
#   4. ADR numbers are unique
#   5. Code fences are language-tagged
# Plus an informational staleness report on last-verified dates.
#
# Conventions live in docs/internals/doc-style.md.
#
# Usage: ./scripts/check_docs.sh

set -euo pipefail

if [ ! -d docs ]; then
    echo "ERROR: docs/ not found. Run from the project root." >&2
    exit 2
fi

python3 - <<'PYEOF'
import os, re, sys, datetime

DOCS = "docs"
errors = 0

def fail(section, msgs):
    global errors
    if msgs:
        errors += 1
        print(f"  FAIL ({len(msgs)}):")
        for m in msgs[:25]:
            print(f"    - {m}")
        if len(msgs) > 25:
            print(f"    ... and {len(msgs) - 25} more")
    else:
        print("  OK")

md_files = []
for dp, _, fs in os.walk(DOCS):
    for f in fs:
        if f.endswith(".md"):
            md_files.append(os.path.join(dp, f))
md_files.sort()

is_generated = lambda p: p.startswith(os.path.join(DOCS, "generated") + os.sep)

# --- 1. Relative links resolve -------------------------------------------
print("--- [1] Relative link resolution ---")
link_re = re.compile(r'\]\(([^)\s#]+)(#[^)\s]*)?\)')
dead = []
for p in md_files:
    text = open(p, encoding="utf-8", errors="replace").read()
    text = re.sub(r'```.*?```', '', text, flags=re.S)
    text = re.sub(r'`[^`\n]*`', '', text)
    for m in link_re.finditer(text):
        tgt = m.group(1)
        if tgt.startswith(("http://", "https://", "mailto:")):
            continue
        if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(p), tgt))):
            dead.append(f"{p}: {tgt}")
fail("links", dead)

# --- 2. Frontmatter present ------------------------------------------------
print("--- [2] Frontmatter (status/audience/last-verified) ---")
missing = []
for p in md_files:
    if is_generated(p):
        continue
    head = open(p, encoding="utf-8", errors="replace").read(400)
    if not head.startswith("---") or "last-verified:" not in head:
        missing.append(p)
fail("frontmatter", missing)

# --- 3. Filename convention -------------------------------------------------
print("--- [3] Filename convention (kebab-case) ---")
name_re = re.compile(r'^[a-z0-9][a-z0-9.-]*\.md$')
release_re = re.compile(r'^Zanna_Release_Notes_[0-9_]+\.md$')
bad_names = []
for p in md_files:
    b = os.path.basename(p)
    if b in ("README.md", "x86_64.md"):  # architecture name keeps its underscore
        continue
    if "release_notes" in p and release_re.match(b):
        continue
    if not name_re.match(b):
        bad_names.append(p)
fail("names", bad_names)

# --- 4. ADR number uniqueness ------------------------------------------------
print("--- [4] ADR number uniqueness ---")
nums = {}
dups = []
for f in sorted(os.listdir(os.path.join(DOCS, "adr"))):
    m = re.match(r'^(\d{4})-', f)
    if m:
        n = m.group(1)
        if n in nums and n != "0000":
            dups.append(f"{n}: {nums[n]} and {f}")
        nums[n] = f
fail("adr", dups)

# --- 5. Code fences are tagged -------------------------------------------------
print("--- [5] Code fence language tags ---")
bare = []
for p in md_files:
    if is_generated(p):
        continue
    in_fence = False
    for i, l in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
        s = l.rstrip()
        if s.startswith("```"):
            if not in_fence:
                if s == "```":
                    bare.append(f"{p}:{i}")
                in_fence = True
            else:
                in_fence = False
fail("fences", bare)

# --- Info: staleness report ---------------------------------------------------
print("--- [info] last-verified staleness (>90 days; ADRs/release notes exempt) ---")
today = datetime.date.today()
stale = []
lv_re = re.compile(r'last-verified:\s*(\d{4}-\d{2}-\d{2})')
for p in md_files:
    if is_generated(p) or "/adr/" in p or "release_notes" in p:
        continue
    m = lv_re.search(open(p, encoding="utf-8", errors="replace").read(400))
    if m:
        d = datetime.date.fromisoformat(m.group(1))
        if (today - d).days > 90:
            stale.append(f"{p} ({m.group(1)})")
for s in stale[:15]:
    print(f"    stale: {s}")
if len(stale) > 15:
    print(f"    ... and {len(stale) - 15} more")
if not stale:
    print("    none")

print()
if errors:
    print(f"=== {errors} CHECK(S) FAILED ===")
    sys.exit(1)
print("=== ALL DOC CHECKS PASSED ===")
PYEOF
