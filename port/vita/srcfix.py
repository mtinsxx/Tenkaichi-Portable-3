"""Rewrites of the game's preprocessed text for the ARM build, with libclang (like port/tools/ptr32.py, whose
helpers this uses).

shifts(): a shift by a count outside 0..31 is undefined in C, and the machines disagree: the PS2 and x86 use the low
5 bits of the count (6 for a 64-bit value), an ARM processor shifts everything out and gives 0. Whether the game
ever shifts out of range is not known, so the question is removed: every shift of the game's text whose count is
not a literal number gets the PS2's rule written out,
        a << n    ->    a << ((n) & 31)          (& 63 when the shifted value is 64 bits wide)
which changes nothing for counts in range and costs one instruction."""
import ptr32
from clang.cindex import CursorKind as K, TokenKind

def shifts(data, args):
    """Returns (new text, number of shifts rewritten). data: bytes of a preprocessed file."""
    game = ptr32.game_test(data)
    tu = ptr32.parse(data, args)
    edits = []  # insertions: (offset, 0, text, order)

    def strip(c):
        while c.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR):
            ch = list(c.get_children())
            if len(ch) != 1:
                break
            c = ch[0]
        return c

    def walk(c, depth):
        for ch in c.get_children():
            walk(ch, depth + 1)
        if c.kind not in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR) or not game(c.extent.start.offset):
            return
        ch = list(c.get_children())
        if len(ch) != 2:
            return
        lo, hi = ch[0].extent.end.offset, ch[1].extent.start.offset
        if hi <= lo or hi - lo > 64:
            return
        op = data[lo:hi].strip(b" \t\r\n()")
        if op not in (b"<<", b">>", b"<<=", b">>="):
            return
        if strip(ch[1]).kind == K.INTEGER_LITERAL:
            return
        wide = (ch[0].type if c.kind == K.COMPOUND_ASSIGNMENT_OPERATOR else c.type).get_canonical().get_size() == 8
        s, e = ch[1].extent.start.offset, ch[1].extent.end.offset
        if e <= s:
            return
        edits.append((s, 0, "((", depth))                                 # of two at one place, the outer one first
        edits.append((e, 0, ") & 63)" if wide else ") & 31)", -depth))    # ... and the inner one closes first
    walk(tu.cursor, 0)
    return ptr32.apply(data, edits), len(edits) // 2
