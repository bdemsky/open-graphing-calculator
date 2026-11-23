/*
 * RPN stack storage (rpn.h): every level is a NUL-terminated text, all levels
 * packed into one caller-supplied buffer, oldest (highest-numbered) level first:
 *
 *     buf: "x^2-2\0" "x\0" "3\0" | free ...
 *           level 3    lvl 2  lvl 1  ^len         ^cap
 *
 * Push and drop only move `len` (level 1 is at the end), so the common stack
 * operations are O(size of the top levels). There is no index of level
 * offsets: level n is found by walking back over n NULs (level_off), which is
 * cheap for the shallow levels RPN commands touch and needs no extra memory.
 * Reordering levels (ROLL, ROLLD) rotates bytes in place, so no temporary
 * buffer is needed either. Pure C without UI or CAS dependencies, unit-tested
 * by tests/unit/test_rpn.c.
 */
#include <string.h>
#include "rpn.h"

void rs_init(rstack_t *s, char *buf, size_t cap)
{
    s->buf = buf;
    s->cap = buf ? cap : 0;
    s->len = 0;
    s->depth = 0;
}

/* Empty the stack (the buffer contents are simply forgotten). */
void rs_clear(rstack_t *s)
{
    s->len = 0;
    s->depth = 0;
}

size_t rs_free(const rstack_t *s) { return s->cap - s->len; }

/* Offset of the first byte of level n (1..depth); n = 0 gives len (the end of
 * the used part), which rs_drop uses as "drop nothing". Walks back from the end:
 * for each level skip its terminating NUL, then its characters. */
static size_t level_off(const rstack_t *s, int n)
{
    size_t p = s->len;
    for (int i = 0; i < n; i++) {
        p--; /* the NUL of this level */
        while (p > 0 && s->buf[p - 1])
            p--;
    }
    return p;
}

const char *rs_get(const rstack_t *s, int level)
{
    if (level < 1 || level > s->depth)
        return NULL;
    return s->buf + level_off(s, level);
}

/* Append text as the new level 1; false (stack unchanged) if it does not fit. */
bool rs_push(rstack_t *s, const char *text)
{
    size_t l = strlen(text) + 1;
    if (l > rs_free(s))
        return false;
    memcpy(s->buf + s->len, text, l);
    s->len += l;
    s->depth++;
    return true;
}

/* Remove levels 1..n (n = 0 is a no-op) by truncating at level n's start. */
bool rs_drop(rstack_t *s, int n)
{
    if (n < 0 || n > s->depth)
        return false;
    s->len = level_off(s, n);
    s->depth -= n;
    return true;
}

/* Push a copy of level n (DUP = 1, OVER = 2). */
bool rs_pick(rstack_t *s, int n)
{
    if (n < 1 || n > s->depth)
        return false;
    size_t off = level_off(s, n);
    size_t l = strlen(s->buf + off) + 1;
    if (l > rs_free(s))
        return false;
    memmove(s->buf + s->len, s->buf + off, l); /* source lies below len: no overlap */
    s->len += l;
    s->depth++;
    return true;
}

/* reverse the bytes of [a, b) in place */
static void reverse(char *a, char *b) /* [a, b) */
{
    while (a < b) {
        char t = *a;
        *a++ = *--b;
        *b = t;
    }
}

/* Rotate [a, end) left by k bytes, in place: the classic three-reversal
 * rotation (reverse both parts, then the whole range; Bentley, "Programming
 * Pearls", column 2). O(end - a) time, no temporary memory. */
static void rotate(char *a, char *end, size_t k)
{
    reverse(a, a + k);
    reverse(a + k, end);
    reverse(a, end);
}

/* ROLL: move level n to level 1, levels n-1..1 move down one (SWAP = 2,
 * ROT = 3). The bytes from level n to the end are rotated left by the size of
 * level n, which puts its text last. */
bool rs_roll(rstack_t *s, int n)
{
    if (n < 1 || n > s->depth)
        return false;
    size_t off = level_off(s, n);
    size_t l = strlen(s->buf + off) + 1;
    rotate(s->buf + off, s->buf + s->len, l);
    return true;
}

/* ROLLD (roll down): the inverse of rs_roll: level 1 becomes level n and
 * levels n..2 move up one. Rotating [level n, end) left by everything except
 * level 1 moves level 1's text to the front of that range. */
bool rs_rolld(rstack_t *s, int n)
{
    if (n < 1 || n > s->depth)
        return false;
    size_t off = level_off(s, n);
    size_t l = s->len - level_off(s, 1); /* size of level 1 */
    rotate(s->buf + off, s->buf + s->len, s->len - off - l);
    return true;
}

/* Replace the text of one level in place (the levels above it move by the
 * size difference). `text` must not point into the stack buffer: the levels
 * are moved before it is copied. */
bool rs_replace(rstack_t *s, int level, const char *text)
{
    if (level < 1 || level > s->depth)
        return false;
    size_t off = level_off(s, level);
    size_t old = strlen(s->buf + off) + 1, nl = strlen(text) + 1;
    if (nl > old && nl - old > rs_free(s))
        return false;
    memmove(s->buf + off + nl, s->buf + off + old, s->len - off - old);
    memcpy(s->buf + off, text, nl);
    s->len = s->len - old + nl;
    return true;
}

/* Copy the whole stack into dst's buffer (the UNDO snapshot); false (dst
 * unchanged) if it does not fit. */
bool rs_copy(rstack_t *dst, const rstack_t *src)
{
    if (src->len > dst->cap)
        return false;
    memcpy(dst->buf, src->buf, src->len);
    dst->len = src->len;
    dst->depth = src->depth;
    return true;
}

/* How many levels, counted from level 1 upwards, fit together in `max` bytes
 * (session saving keeps the most recent levels when the stack is too big). */
int rs_levels_within(const rstack_t *s, size_t max)
{
    int n = 0;
    size_t p = s->len;
    while (n < s->depth) {
        size_t q = p - 1; /* walk back over one level */
        while (q > 0 && s->buf[q - 1])
            q--;
        if (s->len - q > max)
            break;
        p = q;
        n++;
    }
    return n;
}
