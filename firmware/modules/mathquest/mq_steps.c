/*
 * Math Quest: worked examples ("show me how") for every skill.
 *
 * mq_steps(q, p, step, out, vis) describes step `step` of the walkthrough of
 * problem p: up to three lines of text in out[] and the visual model to show
 * in *vis (a copy of p, sometimes changed to show an intermediate stage;
 * vis->a[5] is the step number, which mq_vis.c uses to reveal a model in
 * stages). It returns the number of steps. The last step always states the
 * answer. The walkthrough is paced by the player (tap or ENTER), the model of
 * each step animates in (mq_vis.c reveal), and the problem it explains is a
 * fresh one generated just for it, so the numbers differ every time; a
 * similar problem to try follows (mq_main.c).
 */
#include "mq.h"

#define L(i, ...) api->snprintf(out[i], MQ_TXT, __VA_ARGS__)

int mq_steps(mq_t *q, const mq_prob_t *p, int step, char out[3][MQ_TXT], mq_prob_t *vis)
{
    const modx_api_t *api = q->api;
    char ans[24];
    mq_format(q, p, p->ans, ans, sizeof ans);
    for (int i = 0; i < 3; i++)
        out[i][0] = 0;
    *vis = *p;
    vis->a[5] = (int16_t)step;
    int a = p->a[0], b = p->a[1], n = 3;
    const int16_t *v = p->v;
    switch (p->skill) {
    case S_GROUPS:
    case S_MDWORD:
    case S_TWOSTEP:
    case S_PROPS:
    case S_UNKNOWN:
    case S_DIVFACT:
    case S_FACTORS:
        n = 3;
        if (step == 0) {
            L(0, "Read the story and find the groups.");
            L(1, "Here: %d rows with %d in each row.", v[0], v[1]);
        } else if (step == 1) {
            L(0, "Count by %ds, one row at a time:", v[1]);
            if (v[0] <= 6) {
                char s[MQ_TXT];
                int k = 0;
                s[0] = 0;
                for (int r = 1; r <= v[0]; r++)
                    k += api->snprintf(s + k, sizeof s - (size_t)k, r > 1 ? ", %d" : "%d", r * v[1]);
                L(1, "%s", s);
            } else
                L(1, "%d \xC3\x97 %d = %d", v[0], v[1], v[0] * v[1]);
            if (v[2])
                L(2, "Or split it: %d \xC3\x97 %d + %d \xC3\x97 %d.", v[0], v[2], v[0], v[1] - v[2]);
        } else if (step == 2) {
            if (p->skill == S_TWOSTEP)
                L(0, "Then do the second step of the story.");
            else if (p->skill == S_DIVFACT || p->skill == S_UNKNOWN)
                L(0, "The missing number makes it true.");
            else
                L(0, "Use it to find the answer.");
        }
        break;
    case S_SHARE:
    case S_TIMES_AS:
    case S_MUL10:
        n = 3;
        if (step == 0)
            L(0, "Draw %d circles for the groups.", v[0]);
        else if (step == 1) {
            L(0, "Put %d%s in each circle.", v[1], v[3] == 10 ? " tens" : "");
            L(1, "Every group gets the same amount.");
        } else if (step == 2) {
            if (p->skill == S_SHARE)
                L(0, "Each group has %d: that is the share.", v[1]);
            else if (p->skill == S_MUL10)
                L(0, "%d \xC3\x97 %d tens = %d tens = %d.", v[0], v[1], v[0] * v[1], v[0] * v[1] * 10);
            else
                L(0, "%d groups of %d is %d.", v[0], v[1], v[0] * v[1]);
        }
        break;
    case S_FACTS: {
        char st[3][MQ_TXT];
        mq_fact_strategy(q, p->a[0], st, vis);
        n = 2;
        if (step == 0) {
            L(0, "%s", p->text[0]);
            L(1, "Let's use a trick!");
        } else if (step == 1) {
            for (int i = 0; i < 3; i++)
                api->snprintf(out[i], MQ_TXT, "%s", st[i]);
        }
        break;
    }
    case S_PATTERN:
    case S_RULE:
        n = 2;
        if (step == 0) {
            if (v[1])
                L(0, "Each number is %d more than the one before.", v[1]);
            else if (p->kind == K_CHOICE)
                L(0, "An even number makes pairs with none left.");
            else
                L(0, "Each number is double the one before.");
        } else if (step == 1) {
            L(0, "Keep the rule going, one step at a time.");
            L(1, "Count the steps carefully.");
        }
        break;
    case S_REMWORD:
    case S_DIVIDE:
        n = 3;
        if (step == 0)
            L(0, "Make groups of %d.", p->skill == S_DIVIDE ? b : b);
        else if (step == 1) {
            L(0, "%d full groups fit, with %d left over.", v[1] ? v[1] : (p->ans / 1000), v[2]);
            L(1, "The leftover is less than %d.", p->skill == S_DIVIDE ? b : b);
        } else if (step == 2) {
            if (p->skill == S_REMWORD)
                L(0, "Now read the question: what is asked?");
            else
                L(0, "Write it as quotient R leftover.");
        }
        break;
    case S_PV3:
    case S_PVBIG:
    case S_CMP3:
    case S_CMPBIG:
        n = 3;
        if (step == 0)
            L(0, "Put each digit in its place.");
        else if (step == 1) {
            L(0, "Each place is worth 10 times");
            L(1, "the place to its right.");
        } else if (step == 2) {
            if (p->kind == K_CHOICE)
                L(0, "Compare from the biggest place first.");
            else
                L(0, "Read the value of each place.");
        }
        break;
    case S_ROUND10:
    case S_ROUNDBIG:
        n = 3;
        if (step == 0)
            L(0, "Find the two round numbers around it.");
        else if (step == 1)
            L(0, "Look at the digit just right of the place.");
        else if (step == 2)
            L(0, "5 or more: round up. Less than 5: down.");
        break;
    case S_ADDSUB:
    case S_ADDSUBBIG:
        if (p->a[2] == 2) { /* borrowing across a zero: the regrouping chain */
            n = 4;
            int h = a / 100, o = a % 10;
            if (step == 0) {
                L(0, "Line the numbers up by place.");
                L(1, "Ones first: %d take away %d? Not enough!", o, b % 10);
            } else if (step == 1) {
                L(0, "We need a ten, but there are 0 tens.");
                L(1, "So trade 1 hundred for 10 tens:");
                L(2, "%d hundreds, 10 tens, %d ones.", h - 1, o);
            } else if (step == 2) {
                L(0, "Now trade 1 ten for 10 ones:");
                L(1, "%d hundreds, 9 tens, %d ones.", h - 1, o + 10);
            } else if (step == 3) {
                L(0, "Ones %d\xE2\x88\x92%d=%d, tens 9\xE2\x88\x92%d=%d,", o + 10, b % 10, o + 10 - b % 10, b / 10 % 10,
                  9 - b / 10 % 10);
                L(1, "hundreds %d\xE2\x88\x92%d=%d.", h - 1, b / 100, h - 1 - b / 100);
            }
            break;
        }
        /* fall through */
    case S_MEASWORD:
        n = 3;
        if (step == 0) {
            L(0, "Line the numbers up by place.");
            L(1, "Start with the ones.");
        } else if (step == 1) {
            if (p->a[2] == 0)
                L(0, "Ones make 10 or more? Carry 1 ten.");
            else
                L(0, "Top ones digit too small? Regroup:");
            if (p->a[2])
                L(1, "trade 1 ten for 10 ones.");
        } else if (step == 2)
            L(0, "Then the tens, then the hundreds.");
        break;
    case S_MULTI:
    case S_MULBIG:
        n = 3;
        if (step == 0) {
            L(0, "Split the numbers by place value:");
            L(1, "%d = %d + %d", a, a / 10 * 10, a % 10);
        } else if (step == 1) {
            L(0, "Multiply each part (partial products):");
            if (b > 9)
                L(1, "%d \xC3\x97 %d and %d \xC3\x97 %d", a, b / 10 * 10, a, b % 10);
            else
                L(1, "%d \xC3\x97 %d and %d \xC3\x97 %d", a / 10 * 10, b, a % 10, b);
        } else if (step == 2)
            L(0, "Add the partial products together.");
        break;
    case S_UNITFRAC:
    case S_PARTITION:
        n = 2;
        if (step == 0) {
            L(0, "Count all the equal slices: %d.", v[1]);
            L(1, "That is the bottom number.");
        } else if (step == 1) {
            L(0, "Count the frosted slices.");
            L(1, "That is the top number.");
        }
        break;
    case S_FRACLINE:
        n = 2;
        if (step == 0) {
            L(0, "Each whole is cut into %d equal jumps.", v[2]);
        } else if (step == 1)
            L(0, "Count jumps from 0, not marks.");
        break;
    case S_EQUIV:
    case S_EQUIVMUL:
    case S_TENTHS:
        n = 2;
        if (step == 0) {
            L(0, "Cut every slice into more pieces.");
            L(1, "The amount stays the same.");
        } else if (step == 1)
            L(0, "Multiply top and bottom by the same.");
        break;
    case S_WHOLES:
    case S_MIXED:
        n = 2;
        if (step == 0)
            L(0, "One whole pie = %d slices.", v[1]);
        else if (step == 1)
            L(0, "Count whole pies, then extra slices.");
        break;
    case S_CMPFRAC:
    case S_CMPDIFF:
    case S_FRUNLIKE:
        n = 2;
        if (step == 0) {
            L(0, "Draw both fractions on same-size bars.");
        } else if (step == 1) {
            if (p->skill == S_FRUNLIKE)
                L(0, "Make the slices the same size, then add.");
            else
                L(0, "The longer shaded part is bigger.");
            L(1, "More pieces means smaller pieces.");
        }
        break;
    case S_ADDFRAC:
    case S_FRACWHOLE:
        n = 2;
        if (step == 0)
            L(0, "The slices are all the same size.");
        else if (step == 1)
            L(0, "Work with the top numbers only.");
        break;
    case S_DECIMAL:
    case S_CMPDEC:
    case S_MONEYDEC:
    case S_DEC1000:
        n = 2;
        if (step == 0) {
            L(0, "First place after the point: tenths.");
            L(1, "Second: hundredths. Third: thousandths.");
        } else if (step == 1)
            L(0, "Compare or write place by place.");
        break;
    case S_TIME5:
    case S_TIME1:
        n = 3;
        if (step == 0)
            L(0, "The short hand points to the hour.");
        else if (step == 1)
            L(0, "Count the long hand by 5s from the 12.");
        else if (step == 2)
            L(0, "Then add the extra minute marks.");
        break;
    case S_ELAPSED:
        n = 2;
        if (step == 0)
            L(0, "Jump to the next full hour first.");
        else if (step == 1) {
            L(0, "Then add the rest of the minutes.");
            L(1, "An hour is 60 minutes.");
        }
        break;
    case S_MASSVOL:
    case S_CONVERT:
        n = 2;
        if (step == 0)
            L(0, "Think how big each unit is.");
        else if (step == 1)
            L(0, "Big unit to small unit: multiply.");
        break;
    case S_COINS:
    case S_CHANGE:
    case S_SHOPWORD:
        n = 2;
        if (step == 0)
            L(0, "Start with the biggest coins.");
        else if (step == 1) {
            if (p->skill == S_CHANGE)
                L(0, "Count up from the price to what was paid.");
            else
                L(0, "Count on: 25, 10, 5 and 1 cents.");
        }
        break;
    case S_AREACOUNT:
    case S_AREAMUL:
    case S_FORMULA:
    case S_AREASPLIT:
        n = 3;
        if (step == 0)
            L(0, "Area counts the squares inside.");
        else if (step == 1)
            L(0, "Rows \xC3\x97 squares in a row = area.");
        else if (step == 2)
            L(0, "%d \xC3\x97 %d = %d square units.", v[0], v[1], v[0] * v[1]);
        break;
    case S_AREAL:
        n = 2;
        if (step == 0)
            L(0, "Split the shape into two rectangles.");
        else if (step == 1)
            L(0, "%d + %d = %d", v[0] * v[1], v[2] * v[3], v[0] * v[1] + v[2] * v[3]);
        break;
    case S_PERIM:
        n = 2;
        if (step == 0)
            L(0, "Walk all the way around the edge.");
        else if (step == 1)
            L(0, "Add all four sides: %d + %d + %d + %d.", v[0], v[1], v[0], v[1]);
        break;
    case S_SHAPES:
    case S_CLASSIFY:
    case S_LINES:
    case S_SYMMETRY:
        n = 2;
        if (step == 0)
            L(0, "Count the sides and corners.");
        else if (step == 1)
            L(0, "Check right angles and parallel sides.");
        break;
    case S_ANGLES:
    case S_PROTRACTOR:
    case S_ANGLEADD:
        n = 2;
        if (step == 0)
            L(0, "Put the protractor's middle on the corner.");
        else if (step == 1)
            L(0, "Read the scale that starts at 0.");
        break;
    case S_GRAPH1:
    case S_SCALED:
    case S_TABLE:
    case S_RULER:
    case S_LINEPLOT:
        n = 2;
        if (step == 0)
            L(0, "Find the right bar, row or mark.");
        else if (step == 1) {
            if (p->skill == S_SCALED)
                L(0, "Each step on the scale is %d.", v[1]);
            else
                L(0, "Read across, then answer the question.");
        }
        break;
    case S_VOLUME:
        n = 2;
        if (step == 0)
            L(0, "One layer: %d \xC3\x97 %d = %d cubes.", v[0], v[1], v[0] * v[1]);
        else if (step == 1)
            L(0, "%d layers: %d \xC3\x97 %d = %d.", v[2], v[0] * v[1], v[2], v[0] * v[1] * v[2]);
        break;
    case S_COORD:
        n = 2;
        if (step == 0)
            L(0, "Walk along the bottom: x = %d.", v[0]);
        else if (step == 1)
            L(0, "Then climb up: y = %d.", v[1]);
        break;
    case S_POW10:
        n = 2;
        if (step == 0)
            L(0, "Ten times bigger: every digit moves"), L(1, "one place to the left.");
        else
            L(0, "Ten times smaller: one place right."), L(1, "100 is two places, 1000 is three.");
        break;
    case S_ROUNDDEC:
        n = 2;
        if (step == 0)
            L(0, "Find the place: tenths are 1 after the point,"), L(1, "hundredths 2 after.");
        else
            L(0, "Look at the very next digit:"), L(1, "5 or more rounds up, less stays.");
        break;
    case S_DECOPS:
        n = 2;
        if (step == 0)
            L(0, "Line the points up, one under the other."), L(1, "0.4 is the same as 0.40.");
        else
            L(0, "Then work as with whole numbers"), L(1, "and keep the point in its place.");
        break;
    case S_DIV2:
        n = 2;
        if (step == 0)
            L(0, "Guess, then check with times:"), L(1, "how many groups of the divisor fit?");
        else
            L(0, "Too big? Try one less. Too small?"), L(1, "Try one more. Then times to check.");
        break;
    case S_ORDEROPS:
    case S_EXPR:
        n = 2;
        if (step == 0)
            L(0, "Brackets first: work out what is inside.");
        else
            L(0, "Then times and divide,"), L(1, "then plus and take away.");
        break;
    case S_TWOPAT:
        n = 2;
        if (step == 0)
            L(0, "Find how many days it took"), L(1, "for the first friend.");
        else
            L(0, "The second friend saved for the"), L(1, "same days: times their amount.");
        break;
    case S_FRACWORD:
        n = 2;
        if (step == 0)
            L(0, "Cut both into the same size of slice:"), L(1, "find a bottom number both fit into.");
        else
            L(0, "Now the slices match: add the tops,"), L(1, "keep the bottom.");
        break;
    case S_FRACDIV:
        n = 2;
        if (step == 0)
            L(0, "Sharing a things among b friends:"), L(1, "each gets a/b.");
        else
            L(0, "The top is what is shared,"), L(1, "the bottom how many share it.");
        break;
    case S_FRACMUL:
        n = 2;
        if (step == 0)
            L(0, "A part of a part: multiply"), L(1, "top times top, bottom times bottom.");
        else
            L(0, "The grid shows it: the overlap"), L(1, "is the part of the whole.");
        break;
    case S_SCALING:
        n = 2;
        if (step == 0)
            L(0, "Times less than 1 (like 3/4):"), L(1, "fewer than before.");
        else
            L(0, "Times more than 1 (like 5/4): more."), L(1, "Times exactly 1: the same.");
        break;
    case S_UNITDIV:
        n = 2;
        if (step == 0)
            L(0, "Sharing a small piece makes it smaller:"), L(1, "1/3 shared by 2 is 1/6.");
        else
            L(0, "How many 1/3s fit in 2 wholes?"), L(1, "3 in each whole: 2 x 3 = 6.");
        break;
    case S_CONVERT5:
        n = 2;
        if (step == 0)
            L(0, "Big unit to small unit: multiply."), L(1, "1 m = 100 cm, 1 kg = 1000 g, 1 h = 60 min.");
        else
            L(0, "Small to big: divide."), L(1, "Move the point, keep the amount.");
        break;
    case S_LINEPLOT5:
        n = 2;
        if (step == 0)
            L(0, "Add up all the quarters first:"), L(1, "that is everything there is.");
        else
            L(0, "Then share it fairly:"), L(1, "divide by the number of jars.");
        break;
    case S_SHAPES5:
        n = 2;
        if (step == 0)
            L(0, "A family has rules. A square has 4"), L(1, "right angles: it IS a rectangle.");
        else
            L(0, "A rectangle need not have equal sides:"), L(1, "so not every rectangle is a square.");
        break;
    default:
        n = 1;
        if (step == 0)
            L(0, "Let's work it out together.");
        break;
    }
    if (step == n) {
        L(0, "So the answer is %s.", ans);
        L(1, "You've got this. Your turn next!");
    }
    return n + 1;
}
