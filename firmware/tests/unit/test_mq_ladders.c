/* Sparkle Town (modules/mathquest): the ladders -- every skill a ladder of rungs, one new complication
 * a rung (mq_rung.c), every generator honouring its rung (mq_gen.c):
 *   - the table: 2..MQ_MAXRUNG rungs, a name each, rung r raises exactly one dimension one level;
 *   - honest generators: for every skill and rung, no problem above the rung's levels, most problems
 *     showing the rung's new complication, and no flat rung (the same seed gives other problems one
 *     rung up: the audit's 33 skills that ignored the difficulty);
 *   - the column work recomputed from the numbers: adding and taking away (the brief's ladder: 1-digit,
 *     2+1, 2+2, a carry in the ones, two carries, 3-digit, zeros in the middle, then 4-digit in the next
 *     skill), products and sharing, rung by rung;
 *   - difficulty rises: the numbers' digits per problem, on average, never fall much from a rung to the
 *     next;
 *   - a simulated new player climbing gently: the real game loop through the console, a little less
 *     sure on each rung -- up only after a solid run, never two rungs at once, down after two misses in
 *     a row, the lower half of the ladder in her first orders, about 85 % right.
 * Built and run by `make test`. */
#include "mq_test_util.h"

static int lvmax(int sk, int r, int d) { return mq_rung_max(sk, r, d); }

static void test_table(void)
{
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        int n = mq_rungs(sk);
        CHECK(n >= 2 && n <= MQ_MAXRUNG);
        CHECK(mq_rung_dim(sk, 0) == -1);
        char nm[48];
        for (int r = 0; r < n; r++) {
            CHECK(mq_rung_name(sk, r, nm, sizeof nm) > 0 && nm[0]);
            if (r == 0) {
                for (int d = 0; d < 4; d++)
                    CHECK(lvmax(sk, 0, d) == 0);
                continue;
            }
            int dim = mq_rung_dim(sk, r), up = 0;
            CHECK(dim >= 0 && dim <= 3);
            for (int d = 0; d < 4; d++) {
                int delta = lvmax(sk, r, d) - lvmax(sk, r - 1, d);
                CHECK(delta == (d == dim));
                up += delta;
            }
            CHECK(up == 1); /* one new complication a rung */
            CHECK(mq_rung_band(sk, r) >= mq_rung_band(sk, r - 1));
        }
        CHECK(mq_rung_band(sk, 0) == 0 && mq_rung_band(sk, n - 1) <= 3);
        CHECK(mq_rung_name(sk, n, nm, sizeof nm) == 0);
    }
}

/* the numbers of a problem's text: how many digits they have, all together */
static int load(const mq_prob_t *p)
{
    int n = 0;
    for (int l = 0; l < 3; l++)
        for (const char *s = p->text[l]; *s; s++)
            if (*s >= '0' && *s <= '9')
                n++;
    return n;
}

/* the dimensions that are the size of the numbers */
static bool size_dim(int sk, int d)
{
    static const int8_t sz[][2] = {{S_ADDSUB, 0}, {S_ADDSUBBIG, 0}, {S_MULTI, 0}, {S_DIVIDE, 0}, {S_MULBIG, 0},
                                   {S_GROUPS, 1},  {S_SHARE, 1},    {S_UNKNOWN, 1}, {S_MDWORD, 1}, {S_TWOSTEP, 1},
                                   {S_TIMES_AS, 0}, {S_REMWORD, 1}, {S_FACTORS, 0}, {S_PV3, 0},    {S_PVBIG, 0},
                                   {S_CMPBIG, 1},  {S_ROUNDBIG, 0}, {S_ROUND10, 0}, {S_TABLE, 1},  {S_AREAMUL, 0},
                                   {S_PERIM, 0},   {S_AREACOUNT, 0}};
    for (size_t i = 0; i < sizeof sz / sizeof sz[0]; i++)
        if (sz[i][0] == sk && sz[i][1] == d)
            return true;
    return false;
}

static void test_honest(void)
{
    con("new Hana 2");
    mq_prob_t p, p0;
    int bad = 0, flat = 0, weak = 0;
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        int n = mq_rungs(sk), sum_prev = 0;
        for (int r = 0; r < n; r++) {
            int over = 0, fresh = 0, same = 0, sum = 0, N = 300;
            int dim = mq_rung_dim(sk, r);
            if (sk == S_FACTS)
                con("new Hana 2"); /* (the fact boxes keep their own state) */
            for (int i = 0; i < N; i++) {
                q->rng = (uint32_t)(sk * 7919 + i * 104729 + 3);
                mq_generate(q, &p, sk, r);
                for (int d = 0; d < 4; d++)
                    if (mq_rung_cx(&p, d) > lvmax(sk, r, d)) {
                        if (over++ == 0 && bad < 12)
                            fprintf(stderr, "skill %d (%s) rung %d: level %d in dimension %d (at most %d): %s | %s\n", sk,
                                    mq_skill[sk].name, r, mq_rung_cx(&p, d), d, lvmax(sk, r, d), p.text[0], p.text[1]);
                    }
                if (dim >= 0 && mq_rung_cx(&p, dim) == lvmax(sk, r, dim))
                    fresh++;
                sum += load(&p);
                if (r && sk != S_FACTS) { /* the same seed one rung down */
                    q->rng = (uint32_t)(sk * 7919 + i * 104729 + 3);
                    mq_generate(q, &p0, sk, r - 1);
                    same += mq_prob_ident(&p0) == mq_prob_ident(&p);
                }
            }
            if (over)
                bad++;
            if (r && fresh * 2 < N && sk != S_FACTS) { /* (the fact boxes mix the known tables with the new) */
                if (weak++ < 12)
                    fprintf(stderr, "skill %d (%s) rung %d: its new complication in only %d of %d\n", sk, mq_skill[sk].name,
                            r, fresh, N);
            }
            if (r && same * 2 >= N && sk != S_FACTS) {
                if (flat++ < 12)
                    fprintf(stderr, "skill %d (%s) rung %d: the same problems as rung %d (%d of %d)\n", sk,
                            mq_skill[sk].name, r, r - 1, same, N);
            }
            /* difficulty rises: a rung that makes the numbers bigger never makes them smaller on average (a
             * rung that brings a new kind of question is checked by its levels above) */
            if (r && size_dim(sk, dim) && sum * 100 < sum_prev * 98) {
                if (weak++ < 12)
                    fprintf(stderr, "skill %d (%s) rung %d: the numbers shrink (%d digits after %d)\n", sk,
                            mq_skill[sk].name, r, sum, sum_prev);
            }
            sum_prev = sum;
        }
    }
    CHECK(bad == 0);
    CHECK(weak == 0);
    CHECK(flat == 0);
}

/* the column work of a sum, from its numbers: the addition it is (a - b = c is b + c = a) */
static bool inner0(int32_t n)
{
    for (int32_t t = n; t >= 100; t /= 10)
        if ((t / 10) % 10 == 0)
            return true;
    return false;
}
static void colfeat(const mq_prob_t *p, int *dmax, int *dmin, int *carries, int *ones_only, int *zero)
{
    bool sub = p->a[2] != 0;
    int32_t a = p->op[0], b = p->op[1], x = sub ? b : a, y = sub ? p->ans : b;
    int nx = 1, ny = 1, c = 0, k = 0, mask = 0;
    for (int32_t t = x; t >= 10; t /= 10)
        nx++;
    for (int32_t t = y; t >= 10; t /= 10)
        ny++;
    for (int i = 0; x || y; i++, x /= 10, y /= 10) {
        int s = (int)(x % 10 + y % 10) + c;
        c = s >= 10;
        if (c)
            k++, mask |= 1 << i;
    }
    *dmax = nx > ny ? nx : ny, *dmin = nx < ny ? nx : ny, *carries = k, *ones_only = mask == 1;
    *zero = sub ? inner0(a) : inner0(a) || inner0(b);
}

static void test_columns(void)
{
    mq_prob_t p;
    /* adding and taking away (3.NBT.2), then on in Big add and subtract (4.NBT.4) */
    int nr = mq_rungs(S_ADDSUB);
    CHECK(nr == 7 && mq_rungs(S_ADDSUBBIG) == 5);
    for (int r = 0; r < nr; r++) {
        int ok = 0, fresh = 0, N = 400;
        for (int i = 0; i < N; i++) {
            q->rng = (uint32_t)(i * 2654435761u + (uint32_t)r * 77 + 1);
            mq_generate(q, &p, S_ADDSUB, r);
            int dx, dn, c, ones, z;
            colfeat(&p, &dx, &dn, &c, &ones, &z);
            bool fine = p.ans == (p.a[2] ? p.op[0] - p.op[1] : p.op[0] + p.op[1]);
            fine &= r < 3 || !((p.op[0] == 23 && p.op[1] == 14) || (p.op[0] == 37 && p.op[1] == 23)); /* (never the safe one) */
            /* the levels it reports are the numbers' */
            int size = dx == 1 ? 0 : dx == 2 ? (dn == 1 ? 1 : 2) : 3;
            fine &= mq_rung_cx(&p, 0) == size && mq_rung_cx(&p, 1) == (c > 2 ? 2 : c) && mq_rung_cx(&p, 2) == z;
            switch (r) {
            case 0: fine &= dx == 1 && c == 0, fresh += 1; break;                          /* 1-digit, nothing to carry */
            case 1: fine &= dx <= 2 && dn == 1 && c == 0, fresh += dx == 2; break;         /* 2-digit and 1-digit */
            case 2: fine &= dx <= 2 && c == 0, fresh += dx == 2 && dn == 2; break;          /* 2-digit and 2-digit */
            case 3: fine &= dx <= 2 && c <= 1 && (!c || ones), fresh += c == 1; break;      /* a carry in the ones */
            case 4: fine &= dx <= 2 && c <= 2, fresh += c == 2; break;                      /* two carries */
            case 5: fine &= dx <= 3 && !z && p.ans < 1000 && p.op[0] < 1000, fresh += dx == 3; break; /* 3-digit */
            default: fine &= dx <= 3 && p.op[0] < 1000, fresh += z; break;                  /* zeros in the middle */
            }
            if (!fine && ok++ < 3)
                fprintf(stderr, "add and subtract rung %d: %ld %s %ld = %ld (digits %d/%d, carries %d, zero %d, cx %x)\n", r,
                        (long)p.op[0], p.a[2] ? "-" : "+", (long)p.op[1], (long)p.ans, dx, dn, c, z, p.cx);
        }
        CHECK(ok == 0);
        CHECK(fresh * 2 >= N);
    }
    for (int r = 0; r < mq_rungs(S_ADDSUBBIG); r++) {
        int bad = 0, fresh = 0, N = 300;
        for (int i = 0; i < N; i++) {
            q->rng = (uint32_t)(i * 2654435761u + (uint32_t)r * 99 + 5);
            mq_generate(q, &p, S_ADDSUBBIG, r);
            int dx, dn, c, ones, z;
            colfeat(&p, &dx, &dn, &c, &ones, &z);
            bool fine = dx >= 4 && dx <= (r == 4 ? 5 : 4);
            fine &= r >= 1 || c == 0;
            fine &= r >= 2 || c <= 1;
            fine &= r >= 3 || !z;
            fresh += r == 0 ? 1 : r == 1 ? c == 1 : r == 2 ? c >= 2 : r == 3 ? z : dx == 5;
            bad += !fine;
        }
        CHECK(bad == 0 && fresh * 2 >= N);
    }
    /* products: 2-digit x 1 with nothing to carry, carrying, 3-digit, 4-digit, 2-digit x 2-digit */
    for (int r = 0; r < mq_rungs(S_MULTI); r++) {
        int bad = 0, fresh = 0, N = 300;
        for (int i = 0; i < N; i++) {
            q->rng = (uint32_t)(i * 2654435761u + (uint32_t)r * 13 + 7);
            mq_generate(q, &p, S_MULTI, r);
            int32_t a = p.op[0], b = p.op[1];
            int na = 1, carry = 0;
            for (int32_t t = a; t >= 10; t /= 10)
                na++;
            for (int32_t t = a, c = 0; t; t /= 10) {
                int s = (int)(t % 10) * (int)(b % 10) + c;
                c = s / 10;
                carry |= c > 0;
            }
            bool fine = p.ans == a * b && p.ws == WS_MUL;
            fine &= r >= 1 || !carry;
            fine &= r == 4 ? (b >= 10 || na <= 4) : b < 10;
            fine &= na <= (r <= 1 ? 2 : r == 2 ? 3 : 4);
            fresh += r == 0 ? 1 : r == 1 ? carry : r == 2 ? na == 3 : r == 3 ? na == 4 : b >= 10;
            bad += !fine;
        }
        CHECK(bad == 0 && fresh * 2 >= N);
    }
    /* sharing: 2-digit with every digit sharing, a ten traded, a leftover, 3-digit, 4-digit */
    for (int r = 0; r < mq_rungs(S_DIVIDE); r++) {
        int bad = 0, fresh = 0, N = 300;
        for (int i = 0; i < N; i++) {
            q->rng = (uint32_t)(i * 2654435761u + (uint32_t)r * 31 + 9);
            mq_generate(q, &p, S_DIVIDE, r);
            int32_t n = p.op[0], dv = p.op[1], rem = 0;
            int nd = 1, trades = 0;
            for (int32_t t = n; t >= 10; t /= 10)
                nd++;
            int32_t pw = 1;
            for (int k = 1; k < nd; k++)
                pw *= 10;
            for (int32_t m = pw; m >= 1; m /= 10) {
                int32_t cur = rem * 10 + n / m % 10;
                rem = cur % dv;
                trades += rem && m > 1;
            }
            int32_t qt = p.kind == K_REM ? p.ans / 1000 : p.ans, left = p.kind == K_REM ? p.ans % 1000 : 0;
            bool fine = n == qt * dv + left && left == rem && qt >= 10 && p.ws == WS_DIV;
            fine &= r >= 1 || !trades;
            fine &= r >= 2 || !left;
            fine &= nd <= (r <= 2 ? 2 : r == 3 ? 3 : 4);
            fresh += r == 0 ? 1 : r == 1 ? trades > 0 : r == 2 ? left > 0 : r == 3 ? nd == 3 : nd == 4;
            bad += !fine;
            if (!fine && bad < 3)
                fprintf(stderr, "divide rung %d: %ld / %ld = %ld r %ld (trades %d)\n", r, (long)n, (long)dv, (long)qt,
                        (long)left, trades);
        }
        CHECK(bad == 0 && fresh * 2 >= N);
    }
}

/* ------------------------------------------------------------ a new player climbing */
static uint32_t cl_seed = 12345;
static int cl_rnd(int n)
{
    cl_seed = cl_seed * 1103515245u + 12345u;
    return (int)((cl_seed >> 8) % (uint32_t)n);
}

/* the order is played out: a miss's helper friend, the cheer, the next order */
static void finish_order(void)
{
    for (int j = 0; j < 80 && q->scene == SC_PLAY && (q->sub != GS_PLAY || q->gm.helped); j++) {
        if (q->sub == GS_PLAY && q->gm.helped) {
            con(cl_rnd(100) < 80 || q->gm.hoh ? "correct" : "wrong");
            if (q->sub == GS_CHEER)
                q->gm.helped = 0;
            continue;
        }
        con("next");
    }
}

static void climb(int sk, int orders, int *acc_right, int *acc_n, int *top_seen)
{
    char b[32];
    con("new Lia 2");
    q->fun_off = true;
    mq_profile_t *p = mq_prof(q);
    snprintf(b, sizeof b, "skill %d", sk);
    int hist_r = -1, run = 0, misses = 0, n = 0, right = 0, top = 0, early_hi = 0;
    for (int k = 0; k < orders * 3 && n < orders; k++) {
        if (q->scene != SC_PLAY)
            con(b);
        if (q->sub != GS_PLAY) {
            con("next");
            continue;
        }
        int cur = p->sk[sk].diff, ord = q->prob.diff;
        bool clean = p->sk[sk].tries == p->sk[sk].right;
        bool ok = cl_rnd(100) < 96 - 8 * cur; /* a little less sure a rung up */
        q->now += 3000 + (uint32_t)cl_rnd(3000);
        q->fun_ms = 0;
        bool tw = q->tried_wrong;
        con(ok ? "correct" : "wrong");
        int now = p->sk[sk].diff;

        if (ord == cur && !tw) { /* (her own order at her rung, a first answer: the evidence; an order a friend
                                  * showed her a pattern in comes back to her, answered again) */
            n++, right += ok;
            if (hist_r != cur)
                hist_r = cur, run = 0, misses = 0;
            run = ok ? run + 1 : 0, misses = ok ? 0 : misses + 1;
            CHECK(now - cur <= 1 && cur - now <= 1); /* never two rungs at once */
            if (now > cur)
                CHECK(run >= (clean ? 2 : 4)); /* up only after a solid run */
            if (misses >= 2 && cur > 0)
                CHECK(now < cur); /* two misses in a row: a rung down */
            if (now != cur)
                hist_r = -1;
            if (p->problems <= 40 && mq_rung_band(sk, now) > early_hi)
                early_hi = mq_rung_band(sk, now);
        } else
            CHECK(now == cur); /* an easier one after a miss moves nothing */
        if (now > top)
            top = now;
        finish_order();
    }
    CHECK(early_hi <= 1); /* her first orders: the lower half of the ladder */
    *acc_right += right, *acc_n += n;
    *top_seen = top;
}

static void test_climb(void)
{
    static const int sks[] = {S_ADDSUB, S_GROUPS, S_UNITFRAC, S_TIME1, S_MULTI, S_DIVIDE, S_DECIMAL, S_ADDSUBBIG, S_PV3};
    int right = 0, n = 0;
    for (size_t i = 0; i < sizeof sks / sizeof sks[0]; i++) {
        int top = 0;
        climb(sks[i], 110, &right, &n, &top);
        CHECK(top >= 2);                       /* she does climb */
        CHECK(top <= mq_rungs(sks[i]) - 1);
    }
    if (right * 100 < n * 78 || right * 100 > n * 94)
        fprintf(stderr, "climb: %d of %d right first time\n", right, n);
    CHECK(n > 600 && right * 100 >= n * 78 && right * 100 <= n * 94); /* about 85 % */
}

/* a save from before the ladders: its difficulty 0..3 maps onto the ladder once */
static void test_migrate(void)
{
    con("new Mo 2");
    mq_profile_t *p = mq_prof(q);
    mq_pace_t *pc = mq_pace(q);
    CHECK(pc->rung_ver == 1);
    for (int s = 0; s < MQ_NSKILL; s++)
        p->sk[s].diff = (uint8_t)(s % 4);
    pc->rung_ver = 0;
    mq_rung_migrate(q);
    CHECK(pc->rung_ver == 1);
    for (int s = 0; s < MQ_NSKILL; s++) {
        CHECK(p->sk[s].diff == mq_rung_of_band(s, s % 4) && p->sk[s].diff < mq_rungs(s));
        CHECK(mq_rung_band(s, p->sk[s].diff) <= s % 4 || mq_rungs(s) < 4);
    }
    int before = p->sk[S_ADDSUB].diff;
    mq_rung_migrate(q); /* once only */
    CHECK(p->sk[S_ADDSUB].diff == before);
}

int main(void)
{
    harness_init();
    test_table();
    test_honest();
    test_columns();
    test_migrate();
    test_climb();
    printf("test_mq_ladders: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
