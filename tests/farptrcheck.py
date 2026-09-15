#!/usr/bin/env python3
"""farptrcheck.py -- the banked-memory invariant, checked mechanically.

    farptrcheck.py [src/bdos/*.c src/bdos/*.h]

The BDOS runs in its own segment; a caller (FCB, DMA buffer, command
line, anything reached through an XADDR -- the 32-bit far pointer type
in stdio.h) may be running in a DIFFERENT one.  The only sanctioned way
to touch what an XADDR points at is mem_cpy() or the cpy_in/cpy_out/
cpy_bi wrappers over it (biosdef.h, bdosmisc.c) -- they call map_adr()
first, which is what makes the target segment correct.  Casting an
XADDR straight to a native C pointer and dereferencing it instead
silently truncates it to the BDOS's own segment: on a nonsegmented
caller that is usually harmless (bdosglue.s already folded the caller's
PC segment into the XADDR before any C code sees it, so low halves
often coincide by accident), but for a segmented or split-I/D caller,
or a DMA/search-FCB address left over from a different call, it reads
or writes the wrong bank -- silently.

The check: collect every identifier this tree declares `XADDR` (plain
variables, parameters, struct/union fields -- the type is the
invariant's boundary, deliberately not a hand-picked list of "the
user-facing ones", since telling a caller's XADDR from a BDOS-internal
one by name alone is exactly the judgement call a checker must not be
trusted to make), then flag any place one of those names is cast to a
pointer type -- `(T *)name`, which also covers `*(T *)name` and
`((T *)name)->field` and `((T *)name)[i]` -- because that is the one C
construct capable of dereferencing an XADDR without going through
map_adr.  Passing a bare `name` to mem_cpy/cpy_in/cpy_out/cpy_bi/
map_adr is exactly the sanctioned path and never matches this pattern
(those macros cast to XADDR, not to a pointer type).

SCOPING, and what it actually buys: a brace-depth scan classifies every
`{ ... }` as a struct/union body, a function body (K&R or ANSI, header
included so a K&R parameter declaration line counts as part of the
function, not of file scope), or a plain nested block that inherits
its enclosing function.  A struct/union field named `dmaadr` and an
unrelated local variable named `dmaadr` in some function are DIFFERENT
bindings in C, and are now resolved as different bindings here too:
field declarations only feed the member-access check (`expr.dmaadr`,
`expr->dmaadr`, cast then dereferenced), never the bare-identifier
check, and a function's own locals/parameters shadow same-named
globals for the bare-identifier check within that function's body
(nested blocks are folded into the enclosing function -- true
block scoping is not attempted, so two same-named locals in two
non-overlapping nested blocks of ONE function are conflated; this
tree has no such case today). Two functions may each safely use
`dmaadr` for unrelated things without colliding.

What still escapes this: an XADDR value smuggled into a plain pointer
variable and dereferenced later through THAT variable's own name
(`struct fcb *p = (struct fcb *)x; ... p->foo`) -- the first half is
still `(T *)x` and is caught; a cast and a later, separately-named
dereference is not something a single-pass grep-style tool tracks.
Preprocessor macros that themselves perform the cast are not expanded
(there are none of concern here: cpy_in/cpy_out/cpy_bi/mem_cpy do not
cast to a pointer type). This is source-text scoping, not a compiler's
symbol table -- it trusts brace-matching and K&R declaration syntax,
not #ifdef'd-out branches or macro-generated declarations.
"""

import re
import sys

DECL_RE = re.compile(r'\bXADDR\s+([^;]+);', re.S)
CAST_RE_TMPL = r'\(\s*(?:struct\s+\w+|\w+)\s*\*+\s*\)\s*{0}\b'
MEMBER_CAST_RE_TMPL = (
    r'\(\s*(?:struct\s+\w+|\w+)\s*\*+\s*\)\s*'
    r'[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*\s*(?:\.|->)\s*{0}\b')
STRUCT_KW_RE = re.compile(r'(?:struct|union)(?:\s+\w+)?\s*$')
# A K&R function definition: `name(args)` then zero or more parameter
# declaration lines (no ';' inside them, no nested braces), anchored to
# the END of the span it is searched against -- so a preceding, unrelated
# top-level statement in the same span is left out of group 1.
HEADER_RE = re.compile(
    r'([A-Za-z_]\w*\s*\([^;{}]*\)\s*(?:(?!\{)[^;{}]*;\s*)*[^;{}]*)\Z', re.S)


def strip_comments(text):
    """Blank out /* ... */ bodies, keeping newline count (and hence line
    numbers) intact, so reported line numbers match the original file."""
    out = []
    i = 0
    while True:
        j = text.find('/*', i)
        if j < 0:
            out.append(text[i:])
            break
        out.append(text[i:j])
        k = text.find('*/', j + 2)
        if k < 0:
            break
        out.append('\n' * text.count('\n', j, k + 2))
        i = k + 2
    return ''.join(out)


def names_in(decl_text):
    """Split the captured RHS of an `XADDR ...;` declaration into bare
    identifier names (drop leading '*', array suffix, initializer)."""
    out = []
    for item in decl_text.split(','):
        item = item.strip()
        if not item or '(' in item:
            continue			# function declarations (map_adr(), etc)
        name = re.sub(r'^\*+', '', item)		# EXTERN XADDR *foo
        name = re.split(r'[\[\s=]', name, 1)[0]
        if name.isidentifier():
            out.append(name)
    return out


def scopes(text):
    """Brace-depth scan.  Returns (frames, boundaries) where frames is a
    list of (start, end, kind, func_id) with kind in {'struct', 'func',
    'init', 'block'} -- 'block' frames carry their enclosing func_id too
    -- and boundaries is unused outside this function (kept local)."""
    frames = []
    stack = []		# each: [kind, func_id, start]
    last_frame_end = 0		# just past the previous depth-0 '}'
    func_id = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '{':
            tail = text[max(0, i - 200):i]		# lookback for the kind test
            if STRUCT_KW_RE.search(tail):
                kind, fid, start = 'struct', None, i
            elif re.search(r'=\s*$', tail):
                kind, fid, start = 'init', None, i
            elif not stack:
                # Function header: the name(args) line plus any K&R
                # parameter declaration lines since the last top-level
                # '}' -- found by trimming that span to its trailing
                # "signature + decls" tail, so an unrelated top-level
                # statement earlier in the same span (an extern
                # prototype, a global var) is not swallowed into it.
                since = text[last_frame_end:i]
                m = HEADER_RE.search(since)
                start = last_frame_end + m.start(1) if m else i
                kind, fid = 'func', func_id
                func_id += 1
            else:
                pkind, pfid, _ = stack[-1]
                kind = pkind if pkind in ('struct', 'init') else 'block'
                fid = pfid
                start = i
            stack.append([kind, fid, start])
        elif c == '}':
            if stack:
                kind, fid, start = stack.pop()
                frames.append((start, i + 1, kind, fid))
                if not stack:
                    last_frame_end = i + 1
        i += 1
    return frames


def frame_at(frames, pos):
    """Innermost frame containing pos, or None (true file/top scope)."""
    best = None
    for start, end, kind, fid in frames:
        if start <= pos < end:
            if best is None or (start, -end) > (best[0], -best[1]):
                best = (start, end, kind, fid)
    return best


def analyze(text):
    """Returns (struct_fields, file_globals, func_locals) where
    func_locals maps func_id -> set of XADDR-typed local/param names
    that shadow (are declared, whatever the type, inside that function
    -- only the XADDR ones are kept, but any local declared under the
    same name suppresses inheriting a global's XADDR-ness there)."""
    frames = scopes(text)
    struct_fields = set()
    file_globals = set()
    func_locals = {}		# func_id -> set(name)
    func_shadow = {}		# func_id -> set(name)  (locally re-typed)

    # First: every identifier ANY declaration (not just XADDR) gives a
    # name, per scope, so a local's non-XADDR retyping can shadow.
    all_decl_re = re.compile(
        r'\b(?:[A-Za-z_]\w*\s+)+([A-Za-z_]\w*)\s*(?:\[[^\];]*\])?\s*;')
    for m in all_decl_re.finditer(text):
        pos = m.start()
        fr = frame_at(frames, pos)
        if fr and fr[2] in ('func', 'block'):
            func_shadow.setdefault(fr[3], set()).add(m.group(1))

    for m in DECL_RE.finditer(text):
        pos = m.start()
        fr = frame_at(frames, pos)
        for name in names_in(m.group(1)):
            if fr and fr[2] == 'struct':
                struct_fields.add(name)
            elif fr and fr[2] in ('func', 'block'):
                func_locals.setdefault(fr[3], set()).add(name)
            elif fr is None:
                file_globals.add(name)
            # 'init' frames: not a declaration context, ignore
    return struct_fields, file_globals, func_locals, func_shadow, frames


def linecol(text, pos):
    line = text.count('\n', 0, pos) + 1
    start = text.rfind('\n', 0, pos) + 1
    end = text.find('\n', pos)
    end = len(text) if end < 0 else end
    return line, text[start:end].strip()


def main(argv):
    files = argv[1:] or sys.exit(__doc__)
    texts = {}
    struct_fields = set()
    file_globals = set()
    per_file = {}
    for f in files:
        with open(f) as fh:
            texts[f] = strip_comments(fh.read())
        sf, fg, fl, fsh, frames = analyze(texts[f])
        struct_fields |= sf
        file_globals |= fg
        per_file[f] = (fl, fsh, frames)

    hits = []
    for f in files:
        text = texts[f]
        fl, fsh, frames = per_file[f]
        candidates = set(file_globals)
        for s in fl.values():
            candidates |= s

        # bare-identifier cast: at each match, resolve the name in the
        # scope that contains it -- a function's own locals/params (any
        # type) shadow a same-named global; only a name that is XADDR
        # in the scope that actually binds it counts as a hit.
        for name in candidates:
            for m in re.finditer(CAST_RE_TMPL.format(re.escape(name)), text):
                fr = frame_at(frames, m.start())
                fid = fr[3] if fr and fr[2] in ('func', 'block') else None
                if fid is None:
                    effective = name in file_globals
                else:
                    local_xaddr = fl.get(fid, set())
                    local_other = fsh.get(fid, set()) - local_xaddr
                    if name in local_other:
                        effective = False	# shadowed by a non-XADDR local
                    else:
                        effective = name in local_xaddr or name in file_globals
                if effective:
                    line, src = linecol(text, m.start())
                    hits.append((f, line, src))

        # member-access cast (GBL.dmaadr and the like): struct/union
        # field names are not scoped by function at all -- C resolves
        # them by the expression's type, which this tool does not
        # track, so every declared XADDR field is checked everywhere.
        for name in struct_fields:
            for m in re.finditer(MEMBER_CAST_RE_TMPL.format(re.escape(name)), text):
                line, src = linecol(text, m.start())
                hits.append((f, line, src))

    if hits:
        print("farptrcheck: FAIL -- XADDR value cast to a pointer type "
              "and dereferenced outside cpy_in/cpy_out/cpy_bi/mem_cpy:",
              file=sys.stderr)
        for f, line, src in sorted(set(hits)):
            print("  %s:%d: %s" % (f, line, src), file=sys.stderr)
        print("farptrcheck: each of these bypasses map_adr() -- verify "
              "whether it is really BDOS-local (rename off XADDR) or a "
              "genuine caller-space access (route it through cpy_in/"
              "cpy_out/cpy_bi instead).", file=sys.stderr)
        return 1

    all_locals = set()
    for fl, _, _ in per_file.values():
        for s in fl.values():
            all_locals |= s
    total = len(struct_fields | file_globals | all_locals)
    print("farptrcheck: OK -- %d XADDR-typed identifier(s) (scope-"
          "resolved) across %d file(s), none cast to a pointer type"
          % (total, len(files)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
