"""Small helpers shared by the engine translators: a bracket-aware splitter and
a call rewriter for this CAS's input syntax (and for engine output, which uses
the same function-call notation)."""
import re

IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
OPEN = '([{'
CLOSE = ')]}'


def match_close(s, i):
    """index of the bracket closing the one at s[i]"""
    depth = 0
    j = i
    while j < len(s):
        ch = s[j]
        if ch == '"':
            j = s.index('"', j + 1)
        elif ch in OPEN:
            depth += 1
        elif ch in CLOSE:
            depth -= 1
            if depth == 0:
                return j
        j += 1
    raise ValueError('unbalanced brackets in ' + s)


def split_top(s, sep=','):
    """split at top-level occurrences of sep (outside brackets and strings)"""
    parts, depth, cur, i = [], 0, '', 0
    while i < len(s):
        ch = s[i]
        if ch == '"':
            j = s.index('"', i + 1)
            cur += s[i:j + 1]
            i = j + 1
            continue
        if ch in OPEN:
            depth += 1
        elif ch in CLOSE:
            depth -= 1
        if depth == 0 and s.startswith(sep, i):
            parts.append(cur)
            cur = ''
            i += len(sep)
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


def rewrite_calls(s, fn):
    """Rewrite every call name(args) bottom-up.  fn(name, [arg strings]) returns
    a replacement string, or None to keep the call (with rewritten args)."""
    out = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == '"':
            j = s.index('"', i + 1)
            out.append(s[i:j + 1])
            i = j + 1
            continue
        m = IDENT.match(s, i)
        if m and (i == 0 or not (s[i - 1].isalnum() or s[i - 1] in '_.')):
            name = m.group(0)
            j = m.end()
            if j < len(s) and s[j] == '(':
                k = match_close(s, j)
                inner = s[j + 1:k]
                args = [rewrite_calls(a, fn) for a in split_top(inner)] if inner.strip() else []
                rep = fn(name, args)
                out.append(rep if rep is not None else '%s(%s)' % (name, ','.join(args)))
                i = k + 1
                continue
            out.append(name)
            i = j
            continue
        if ch.isdigit():
            m2 = re.match(r'[0-9A-Za-z_.]+', s[i:])     # 0xABC, 1e-5, k12 already handled
            out.append(m2.group(0))
            i += len(m2.group(0))
            continue
        out.append(ch)
        i += 1
    return ''.join(out)


def rename_idents(s, table):
    """rename bare identifiers (not followed by '(' necessarily) by a table"""
    def rep(m):
        st = m.start()
        if st > 0 and (s[st - 1].isalnum() or s[st - 1] in '_.%'):
            return m.group(0)
        return table.get(m.group(0), m.group(0))
    return IDENT.sub(rep, s)


def call_names(s):
    """names used as functions in s"""
    names = set()
    rewrite_calls(s, lambda nm, a: names.add(nm))
    return names


def top_equal(s):
    """split 'lhs=rhs' at a top-level single '=' (not <=, >=, :=, ==, !=)"""
    depth = 0
    for i, ch in enumerate(s):
        if ch in OPEN:
            depth += 1
        elif ch in CLOSE:
            depth -= 1
        elif ch == '=' and depth == 0 and (i == 0 or s[i - 1] not in '<>!:=') and \
                (i + 1 >= len(s) or s[i + 1] != '='):
            return s[:i], s[i + 1:]
    return None


def strip_outer(s):
    s = s.strip()
    while s.startswith('(') and match_close(s, 0) == len(s) - 1:
        s = s[1:-1].strip()
    return s


def replace_call_terms(s, name, value='0'):
    """replace every call name(...) (whole call) by value"""
    return rewrite_calls(s, lambda nm, a: value if nm == name else None)
