/*
 * mem.c -- arena layout, scratch bump allocator with compaction, persistent
 * free-list heap, error handling and small freestanding utilities.
 *
 * Arena layout (all carved from the block given to cas_init):
 *   [cas_ctx][symbol table + permanent nodes][persistent heap][scratch]
 * (cas_init in cas.c does the carving; this file manages the two dynamic
 * regions.)  The library never calls malloc: every byte comes from here.
 *
 * Two allocators with very different lifetimes:
 *
 *  1. Scratch: a bump ("region") allocator.  Allocation is a pointer
 *     increment; there is no free.  The whole region is reset at the start
 *     and end of every API call (cas.c), and algorithms bound their memory
 *     with marks: smark() remembers the top, srelease(mark) drops everything
 *     allocated since, and skeep(mark, r) drops everything since `mark`
 *     EXCEPT the expression r, which is compacted down to `mark`.  skeep is
 *     a miniature copying garbage collector (Cheney 1970: copy reachable
 *     nodes, leave forwarding pointers in the old copies) followed by a
 *     slide of the copies down to the mark and a pointer fix-up pass.
 *     Loops such as gcd, Hensel lifting or quadrature call it once per
 *     iteration so they run in constant memory.
 *
 *  2. Persistent heap: a classic first-fit allocator over an address-ordered
 *     free list with immediate coalescing of neighbours (Knuth, TAOCP vol. 1,
 *     sec. 2.5).  It holds only values that outlive an evaluation: variable
 *     values, user functions, ans and compiled functions.  An expression is
 *     stored by hstore() as one block containing a sharing-preserving deep
 *     copy, so freeing a value is a single hfree().
 *
 * Invariant relied on by skeep: nodes are immutable and children are always
 * created before their parents, so a node never points to a node allocated
 * after it; in particular nothing below a mark points above it.
 *
 * Errors use setjmp/longjmp: cas_throw records a status and a message in the
 * context and jumps to the innermost handler (a tryf frame pushed by
 * try_enter).  The same file hosts the recursion guard (stack_check) and the
 * interrupt / poll checks, which are made from the allocator so that every
 * long-running computation notices them.
 */
#include "cas_int.h"
#include <string.h>

cas_ctx *K;

/* round a byte count up to a multiple of 8 (alignment of doubles/pointers) */
#define ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

/* ------------------------------------------------------------ errors --- */
/* Raise an error: store `st` and a copy of `msg` (truncated to fit
 * K->errmsg; NULL gives an empty message) in the context, then longjmp to the
 * innermost handler K->tf.  Never returns.  Requires a handler to be
 * installed; every public entry point installs one before calling into the
 * engine.  Scratch allocations are not undone; the handler decides what to
 * release. */
_Noreturn void cas_throw(cas_status st, const char *msg) {
    K->err = st;
    size_t i = 0;
    if (msg)
        for (; msg[i] && i + 1 < sizeof K->errmsg; i++) K->errmsg[i] = msg[i];
    K->errmsg[i] = 0;
    longjmp(K->tf->jb, 1);
}

/* Re-raise the error already recorded in K (after a local handler decided it
 * cannot recover); jumps to the handler that is now innermost. */
void rethrow(void) { longjmp(K->tf->jb, 1); }

/* Push / pop handler frame t (see the idiom in cas_int.h).  The caller must
 * call setjmp(t->jb) itself, in its own stack frame, because a jmp_buf filled
 * in a function that has returned cannot be jumped to. */
void try_enter(tryf *t) {
    t->prev = K->tf;
    K->tf = t;
}
void try_leave(tryf *t) { K->tf = t->prev; }

/* cas_set_poll: ask the application every 1024 checks whether to stop.
 * A nonzero answer sets K->intr, which the caller then turns into
 * CAS_ERR_INTERRUPTED.  Called from salloc, stack_check and poll_intr, so the
 * callback frequency is tied to allocation / recursion activity, not time. */
void cas_poll_step(void) {
    if (K->poll && !(++K->poll_n & 1023u) && K->poll(K->poll_user)) K->intr = 1;
}

/* Recursion guard, called at the top of every deep recursion.  The C stack
 * use is estimated as the distance between a local of the API entry point
 * (K->stack_base, set in cas.c) and a local here; this assumes a downward
 * growing stack, as on ARM and all host targets.  Exceeding K->stack_limit
 * throws CAS_ERR_MEMORY ("too deeply nested") instead of overflowing the
 * stack; the check is skipped when no entry point set stack_base.  Also polls
 * for interrupts, since deep recursion may allocate little. */
void stack_check(void) {
    char here;
    if (K->poll) cas_poll_step();
    if (K->stack_base && (size_t)(K->stack_base - &here) > K->stack_limit)
        cas_throw(CAS_ERR_MEMORY, "expression too deeply nested");
    if (K->intr) cas_throw(CAS_ERR_INTERRUPTED, "interrupted");
}

/* ----------------------------------------------------------- scratch --- */
/* Bump-allocate n bytes (rounded up to 8) from scratch.  O(1).  Throws
 * CAS_ERR_MEMORY when the region is exhausted and CAS_ERR_INTERRUPTED when
 * an interrupt is pending; tracks the peak for cas_mem.  The memory is
 * uninitialised and lives until the top is released below it. */
void *salloc(size_t n) {
    n = ALIGN8(n);
    if (K->poll) cas_poll_step();
    if (K->intr) cas_throw(CAS_ERR_INTERRUPTED, "interrupted");
    if ((size_t)(K->s_hi - K->s_top) < n + K->s_reserve) cas_throw(CAS_ERR_MEMORY, "out of memory");
    void *p = K->s_top;
    K->s_top += n;
    size_t used = (size_t)(K->s_top - K->s_lo);
    if (used > K->s_peak) K->s_peak = used;
    return p;
}

/* Allocate a node of type t with `payload` bytes after the header; header
 * fields are zeroed (flags cleared, n = 0), the payload is not.  Every node
 * is made at least large enough to hold a forwarding pointer (see skeep). */
E *ealloc(int t, size_t payload) {
    size_t n = sizeof(E) + payload;
    if (n < sizeof(E) + sizeof(void *)) n = sizeof(E) + sizeof(void *);
    E *e = salloc(n);
    e->t = (uint8_t)t;
    e->f = 0;
    e->s = 0;
    e->n = 0;
    return e;
}

/* Reset the scratch top to mark m, discarding everything allocated after it.
 * Marks outside [s_lo, s_top] (e.g. a stale mark that is already above the
 * top) are ignored. */
void srelease(char *m) {
    if (m >= K->s_lo && m <= K->s_top) K->s_top = m;
}

/* node types whose payload is an array of child pointers */
static int is_compound(int t) {
    return t == E_RAT || t == E_ADD || t == E_MUL || t == E_POW || t == E_FN || t == E_LIST;
}

/* Total size in bytes of node e as allocated by ealloc (header + payload,
 * minimum header + one pointer, rounded to 8).  Must agree with how every
 * node type is built; skeep walks the compacted region using it.  An E_FLT
 * with n > 0 is an arbitrary-precision float carrying an 8-byte exponent and
 * digits header plus n mantissa limbs after the double (bf.c). */
static size_t node_size(const E *e) {
    size_t n;
    switch (e->t) {
    case E_INT: n = sizeof(E) + sizeof(limb) * e->n; break;
    case E_FLT: n = sizeof(E) + sizeof(double) + (e->n ? 8 + sizeof(limb) * e->n : 0); break;
    case E_STR: n = sizeof(E) + e->n + 1; break;
    case E_SYM: n = sizeof(E); break;
    default: n = sizeof(E) + sizeof(E *) * e->n; break;
    }
    if (n < sizeof(E) + sizeof(void *)) n = sizeof(E) + sizeof(void *);
    return ALIGN8(n);
}

/* p lies in the part of scratch being released by skeep(mark, ...) */
static int in_scratch_above(const void *p, const char *mark) {
    return (const char *)p >= mark && (const char *)p < K->s_top;
}

/* Copy phase of skeep: return a copy of e allocated at the current scratch
 * top, recursively copying children that also live above `mark` (children
 * below the mark, in the heap or permanent are shared, not copied).  Each
 * copied original is overwritten with an E_FWD forwarding pointer to its
 * copy, so a node reachable along several paths is copied exactly once and
 * DAG sharing is preserved.  Recursion depth = expression depth (guarded by
 * stack_check). */
static E *keep_copy(E *e, char *mark) {
    if (!in_scratch_above(e, mark)) return e;
    if (e->t == E_FWD) return *(E **)(e + 1);
    stack_check();
    size_t sz = node_size(e);
    E *c = salloc(sz);
    memcpy(c, e, sz);
    /* forward the old node (it lives in the region being discarded) */
    E *old = e;
    if (is_compound(c->t)) {
        old->t = E_FWD;
        *(E **)(old + 1) = c;
        for (uint32_t i = 0; i < c->n; i++) EK(c)[i] = keep_copy(EK(c)[i], mark);
    } else {
        old->t = E_FWD;
        *(E **)(old + 1) = c;
    }
    return c;
}

/* Release scratch above `mark` but keep expression r (see the file header).
 * Returns r's new address; the caller must use it instead of r, and every
 * other pointer into the released region becomes invalid.  If r is NULL or
 * does not live above the mark (e.g. a constant or an old node) the call is a
 * plain srelease(mark).  Cost is linear in the size of the kept expression.
 *
 * Steps:
 *   1. keep_copy: copy r's live nodes above the old top [top0, top1),
 *      forwarding the originals;
 *   2. memmove the copies down to `mark` (the garbage in between vanishes);
 *   3. walk the moved nodes in address order and subtract `delta` from every
 *      child pointer that pointed into the copy area.
 */
E *skeep(char *mark, E *r) {
    if (!r || !in_scratch_above(r, mark)) {
        if (!r) K->s_top = mark;
        else if ((char *)r < mark) K->s_top = mark;
        return r;
    }
    char *top0 = K->s_top;
    E *nr = keep_copy(r, mark);
    char *top1 = K->s_top;
    size_t len = (size_t)(top1 - top0);
    size_t delta = (size_t)(top0 - mark);
    memmove(mark, top0, len);
    /* fix internal pointers */
    char *p = mark, *end = mark + len;
    while (p < end) {
        E *e = (E *)p;
        if (is_compound(e->t))
            for (uint32_t i = 0; i < e->n; i++) {
                char *c = (char *)EK(e)[i];
                if (c >= top0 && c < top1) EK(e)[i] = (E *)(c - delta);
            }
        p += node_size(e);
    }
    K->s_top = mark + len;
    return (E *)((char *)nr - delta);
}

/* ---------------------------------------------------- persistent heap --- */
/* Header of every heap block, allocated or free.  The header slot is a fixed
 * HHDR = 16 bytes on all targets (sizeof(hblk) is 8 on 32-bit ARM and 16 on
 * 64-bit hosts); the payload follows it.  `next` is meaningful only while the
 * block is free. */
struct hblk {
    size_t size; /* total block size including header */
    hblk *next;
};
#define HHDR 16

/* First-fit allocation of n bytes from the persistent heap.  The free list
 * is scanned in address order; the first block that is large enough is used,
 * splitting off the tail as a new free block when at least 64 bytes would
 * remain (smaller remainders are left inside the allocation to avoid useless
 * fragments).  Throws CAS_ERR_MEMORY when no block fits.  O(#free blocks). */
void *halloc(size_t n) {
    n = ALIGN8(n) + HHDR;
    if (n < sizeof(hblk) + 16) n = sizeof(hblk) + 16;
    hblk **pp = &K->freel;
    while (*pp) {
        hblk *b = *pp;
        if (b->size >= n) {
            if (b->size - n >= 64) {
                hblk *rest = (hblk *)((char *)b + n);
                rest->size = b->size - n;
                rest->next = b->next;
                *pp = rest;
                b->size = n;
            } else {
                *pp = b->next;
            }
            K->h_used += b->size;
            if (K->h_used > K->h_peak) K->h_peak = K->h_used;
            return (char *)b + HHDR;
        }
        pp = &b->next;
    }
    cas_throw(CAS_ERR_MEMORY, "out of memory (variable storage)");
}

/* Return a block to the heap.  The block is inserted at its address
 * position in the free list and merged with the following and the preceding
 * free block when they are adjacent, so free space never stays fragmented
 * into neighbouring pieces.  NULL and pointers outside the heap (e.g.
 * permanent nodes) are ignored.  O(#free blocks). */
void hfree(void *p) {
    if (!p) return;
    hblk *b = (hblk *)((char *)p - HHDR);
    if ((char *)b < K->h_lo || (char *)b >= K->h_hi) return;
    K->h_used -= b->size;
    hblk **pp = &K->freel;
    while (*pp && *pp < b) pp = &(*pp)->next;
    b->next = *pp;
    *pp = b;
    /* coalesce with next */
    if (b->next && (char *)b + b->size == (char *)b->next) {
        b->size += b->next->size;
        b->next = b->next->next;
    }
    /* coalesce with previous */
    if (pp != &K->freel) {
        hblk *prev = (hblk *)((char *)pp - offsetof(hblk, next));
        if ((char *)prev + prev->size == (char *)b) {
            prev->size += b->size;
            prev->next = b->next;
        }
    }
}

size_t heap_used(void) { return K->h_used; }

/* deep copy into one heap block, preserving sharing
 *
 * pmap is an open-addressing (linear probing) hash map from original node
 * address to copy, kept at most half full; it lives in scratch and is
 * released when hstore returns.  Keys = nodes visited, vals = their copies. */
typedef struct {
    E **keys;
    E **vals;
    size_t cap, cnt;
} pmap;

/* Nodes below the heap (symbol nodes, small constants built by cas_init)
 * live for the whole session and are referenced rather than copied. */
static int is_permanent(const E *e) {
    return (const char *)e < K->h_lo; /* symbol nodes and constants */
}

/* Slot of key k: either the slot holding k or the empty slot where it would
 * be inserted.  Multiplicative (Fibonacci-style) hashing of the address;
 * cap is a power of two. */
static size_t pm_slot(pmap *m, E *k) {
    size_t h = ((uintptr_t)k >> 3) * 2654435761u;
    size_t i = h & (m->cap - 1);
    while (m->keys[i] && m->keys[i] != k) i = (i + 1) & (m->cap - 1);
    return i;
}

/* Double the table (initially 256 slots) and rehash; the old arrays are
 * simply abandoned in scratch. */
static void pm_grow(pmap *m) {
    pmap n;
    n.cap = m->cap ? m->cap * 2 : 256;
    n.cnt = 0;
    n.keys = salloc(sizeof(E *) * n.cap);
    n.vals = salloc(sizeof(E *) * n.cap);
    memset(n.keys, 0, sizeof(E *) * n.cap);
    for (size_t i = 0; i < m->cap; i++)
        if (m->keys[i]) {
            size_t s = pm_slot(&n, m->keys[i]);
            n.keys[s] = m->keys[i];
            n.vals[s] = m->vals[i];
            n.cnt++;
        }
    *m = n;
}

/* Pass 1 of hstore: total bytes needed for the distinct non-permanent nodes
 * reachable from e.  Each node is entered into the map the first time it is
 * seen, so shared subexpressions are counted once. */
static size_t hs_size(pmap *m, E *e) {
    if (is_permanent(e)) return 0;
    if (m->cnt * 2 >= m->cap) pm_grow(m);
    size_t s = pm_slot(m, e);
    if (m->keys[s]) return 0;
    stack_check();
    m->keys[s] = e;
    m->vals[s] = NULL;
    m->cnt++;
    size_t sz = node_size(e);
    if (is_compound(e->t))
        for (uint32_t i = 0; i < e->n; i++) sz += hs_size(m, EK(e)[i]);
    return sz;
}

/* Pass 2 of hstore: copy e and its children into the block at *dst
 * (advancing it), reusing the copy recorded in the map for nodes already
 * copied.  The map built by pass 1 guarantees every visited key has a slot. */
static E *hs_copy(pmap *m, E *e, char **dst) {
    if (is_permanent(e)) return e;
    size_t s = pm_slot(m, e);
    if (m->vals[s]) return m->vals[s];
    size_t sz = node_size(e);
    E *c = (E *)*dst;
    *dst += sz;
    memcpy(c, e, sz);
    m->vals[s] = c;
    if (is_compound(e->t))
        for (uint32_t i = 0; i < e->n; i++) EK(c)[i] = hs_copy(m, EK(e)[i], dst);
    return c;
}

/* Store expression e persistently: returns a deep copy that lives in a single
 * halloc block and shares exactly the subexpressions e shares.  The result
 * does not depend on scratch, so it survives the end of the evaluation; free
 * it with hfree_expr.  Nodes already in the heap (e.g. another variable's
 * value) are copied as well, so blocks never point into each other.  Throws
 * CAS_ERR_MEMORY if the heap is full.  Scratch used for the map is
 * released before returning. */
E *hstore(E *e) {
    if (!e) return NULL;
    if (is_permanent(e)) {
        /* still need an owning block so hfree_expr works uniformly */
        E *c = halloc(node_size(e));
        memcpy(c, e, node_size(e));
        return c;
    }
    char *mark = smark();
    pmap m = {0, 0, 0, 0};
    pm_grow(&m);
    size_t total = hs_size(&m, e);
    char *blk = halloc(total);
    char *dst = blk;
    E *r = hs_copy(&m, e, &dst);
    srelease(mark);
    return r;
}

/* persistent values may still be referenced from scratch expressions of the
 * running evaluation: frees are deferred until the evaluation ends */
typedef struct pfree {
    void *p;
    struct pfree *next;
} pfree;

/* Free a value created by hstore.  If scratch is empty nothing can refer to
 * the value and it is freed at once; otherwise a pending-free record (itself
 * in scratch) is pushed onto K->pending and the block is released by
 * flush_frees() when the API call finishes. */
void hfree_expr(E *e) {
    if (!e) return;
    if (!K->s_lo || K->s_top == K->s_lo) {
        /* nothing in scratch can reference it */
        hfree(e);
        return;
    }
    pfree *f = salloc(sizeof(pfree));
    f->p = e;
    f->next = K->pending;
    K->pending = f;
}

/* Perform the frees deferred by hfree_expr (called by cas.c at the end of
 * every API call, before scratch is reset). */
void flush_frees(void) {
    pfree *f = K->pending;
    K->pending = NULL;
    while (f) {
        hfree(f->p);
        f = f->next;
    }
}

/* --------------------------------------------------------- utilities --- */
/* <string.h> wrappers declared in cas_int.h */
size_t c_strlen(const char *s) { return strlen(s); }
int c_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int c_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
void c_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void c_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void c_memset(void *d, int c, size_t n) { memset(d, c, n); }

/* Marsaglia's xorshift32 generator (shifts 13, 17, 5; period 2^32 - 1).
 * The state is reseeded at every cas_eval, so randomized algorithms give
 * reproducible results for the same input. */
uint32_t rng_next(void) {
    uint32_t x = K->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    K->rng = x;
    return x;
}

/* Append one character, keeping room for the terminating NUL; when the
 * buffer is full the character is dropped and b->overflow set. */
void sb_putc(sbuf *b, char c) {
    if (b->len + 1 < b->cap) {
        b->buf[b->len++] = c;
        b->buf[b->len] = 0;
    } else
        b->overflow = 1;
}
void sb_putn(sbuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) sb_putc(b, s[i]);
}
void sb_puts(sbuf *b, const char *s) {
    while (*s) sb_putc(b, *s++);
}
/* Append the decimal representation of v (LONG_MIN safe: the magnitude is
 * computed in unsigned arithmetic). */
void sb_int(sbuf *b, long v) {
    char t[24];
    int n = 0;
    unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
    do {
        t[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) sb_putc(b, '-');
    while (n) sb_putc(b, t[--n]);
}
