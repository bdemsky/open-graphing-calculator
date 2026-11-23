/*
 * Math Quest: the curriculum tables.
 *
 * mq_std[]   every Common Core standard of grades 3 and 4 (3.OA.1-9, 3.NBT.1-3,
 *            3.NF.1-3, 3.MD.1-8, 3.G.1-2, 4.OA.1-5, 4.NBT.1-6, 4.NF.1-7,
 *            4.MD.1-7, 4.G.1-3), the grade-2 foundations a grade-3/4 student
 *            meets in adaptive assessments (place value and +/- within 1000,
 *            time to 5 minutes, money, picture and bar graphs) and the grade-5
 *            challenge track (multi-digit multiplication, unlike denominators,
 *            thousandths, volume, the coordinate plane). Each row has its
 *            grade, its Star Math / Core Progress domain, a short description
 *            and the skills (problem generators, mq_gen.c) that teach and
 *            practise it. A standard is passed when all of its skills are
 *            (skill mastery, mq_main.c). tests/unit/test_mathquest.c fails
 *            the build when a standard has no skill, a skill is not on any
 *            level, or a required standard id is missing.
 * mq_skill[] name, world and grade of each skill (index = S_*).
 * mq_levels[] the levels of each world in play order (a boss follows them).
 * mq_mc_*    the misconceptions: a gentle name and two lines of help.
 *
 * Constant tables of characters only (no pointers: the image is
 * position-independent).
 */
#include "mq.h"

#define SK(a) (uint8_t)((a) + 1)
const mq_std_t mq_std[MQ_NSTD] = {
    /* grade 2 foundations */
    {"2.NBT.1", 2, DOM_NO, "Hundreds, tens and ones in a three-digit number", {SK(S_PV3)}},
    {"2.NBT.3", 2, DOM_NO, "Read and write numbers to 1000, expanded form", {SK(S_PV3), SK(S_CMP3)}},
    {"2.NBT.4", 2, DOM_NO, "Compare three-digit numbers with >, = and <", {SK(S_CMP3)}},
    {"2.NBT.7", 2, DOM_NO, "Add and subtract within 1000 with models", {SK(S_ADDSUB)}},
    {"2.MD.7", 2, DOM_MD, "Tell and write time to the nearest five minutes", {SK(S_TIME5)}},
    {"2.MD.8", 2, DOM_MD, "Money: dollar bills, quarters, dimes, nickels, pennies", {SK(S_COINS), SK(S_CHANGE)}},
    {"2.MD.10", 2, DOM_MD, "Picture and bar graphs with single-unit scales", {SK(S_GRAPH1)}},
    /* grade 3 */
    {"3.OA.1", 3, DOM_AT, "Products as equal groups: 5 x 7 is 5 groups of 7", {SK(S_GROUPS), SK(S_FACTS)}},
    {"3.OA.2", 3, DOM_AT, "Quotients as fair shares or number of groups", {SK(S_SHARE)}},
    {"3.OA.3", 3, DOM_AT, "Multiply and divide within 100 in word problems", {SK(S_MDWORD)}},
    {"3.OA.4", 3, DOM_AT, "Find the unknown number in a x or / equation", {SK(S_UNKNOWN)}},
    {"3.OA.5", 3, DOM_AT, "Properties: commutative, associative, distributive", {SK(S_PROPS)}},
    {"3.OA.6", 3, DOM_AT, "Division as an unknown-factor problem", {SK(S_DIVFACT)}},
    {"3.OA.7", 3, DOM_AT, "Fluently multiply and divide within 100", {SK(S_FACTS)}},
    {"3.OA.8", 3, DOM_AT, "Two-step word problems, check by estimating", {SK(S_TWOSTEP), SK(S_SHOPWORD)}},
    {"3.OA.9", 3, DOM_AT, "Arithmetic patterns and properties (even, odd)", {SK(S_PATTERN)}},
    {"3.NBT.1", 3, DOM_NO, "Round whole numbers to the nearest 10 or 100", {SK(S_ROUND10)}},
    {"3.NBT.2", 3, DOM_NO, "Fluently add and subtract within 1000", {SK(S_ADDSUB)}},
    {"3.NBT.3", 3, DOM_NO, "Multiply one-digit numbers by multiples of 10", {SK(S_MUL10)}},
    {"3.NF.1", 3, DOM_FD, "Unit fractions 1/b and fractions a/b as parts", {SK(S_UNITFRAC)}},
    {"3.NF.2", 3, DOM_FD, "Fractions on a number line diagram", {SK(S_FRACLINE)}},
    {"3.NF.3", 3, DOM_FD, "Equivalent fractions, wholes, compare same num/den",
     {SK(S_EQUIV), SK(S_WHOLES), SK(S_CMPFRAC)}},
    {"3.MD.1", 3, DOM_MD, "Time to the nearest minute and elapsed time", {SK(S_TIME1), SK(S_ELAPSED)}},
    {"3.MD.2", 3, DOM_MD, "Liquid volume and mass in g, kg and l", {SK(S_MASSVOL)}},
    {"3.MD.3", 3, DOM_MD, "Scaled picture and bar graphs; read tables", {SK(S_SCALED), SK(S_TABLE)}},
    {"3.MD.4", 3, DOM_MD, "Measure to 1/2 and 1/4 inch; line plots", {SK(S_RULER)}},
    {"3.MD.5", 3, DOM_GE, "Area: unit squares cover a plane figure", {SK(S_AREACOUNT)}},
    {"3.MD.6", 3, DOM_GE, "Measure area by counting unit squares", {SK(S_AREACOUNT)}},
    {"3.MD.7", 3, DOM_GE, "Area by multiplying, splitting and L-shapes",
     {SK(S_AREAMUL), SK(S_AREASPLIT), SK(S_AREAL)}},
    {"3.MD.8", 3, DOM_GE, "Perimeter, unknown side lengths, same area", {SK(S_PERIM)}},
    {"3.G.1", 3, DOM_GE, "Shape categories: rhombus, rectangle, square", {SK(S_SHAPES)}},
    {"3.G.2", 3, DOM_GE, "Partition shapes into equal areas as unit fractions", {SK(S_PARTITION)}},
    /* grade 4 */
    {"4.OA.1", 4, DOM_AT, "Multiplication as comparison: 35 is 5 times 7", {SK(S_TIMES_AS)}},
    {"4.OA.2", 4, DOM_AT, "Multiplicative comparison word problems", {SK(S_TIMES_AS)}},
    {"4.OA.3", 4, DOM_AT, "Multistep word problems, interpret remainders", {SK(S_REMWORD)}},
    {"4.OA.4", 4, DOM_AT, "Factor pairs, multiples, prime and composite", {SK(S_FACTORS)}},
    {"4.OA.5", 4, DOM_AT, "Number and shape patterns that follow a rule", {SK(S_RULE)}},
    {"4.NBT.1", 4, DOM_NO, "A digit is ten times the value of the place to its right", {SK(S_PVBIG)}},
    {"4.NBT.2", 4, DOM_NO, "Read, write, compare multi-digit numbers", {SK(S_CMPBIG)}},
    {"4.NBT.3", 4, DOM_NO, "Round multi-digit numbers to any place", {SK(S_ROUNDBIG)}},
    {"4.NBT.4", 4, DOM_NO, "Fluently add and subtract multi-digit numbers", {SK(S_ADDSUBBIG)}},
    {"4.NBT.5", 4, DOM_NO, "Multiply 4 digits by 1 digit, 2 digits by 2 digits", {SK(S_MULTI)}},
    {"4.NBT.6", 4, DOM_NO, "Divide up to 4 digits by 1 digit, with remainders", {SK(S_DIVIDE)}},
    {"4.NF.1", 4, DOM_FD, "Equivalent fractions: (n x a)/(n x b)", {SK(S_EQUIVMUL)}},
    {"4.NF.2", 4, DOM_FD, "Compare fractions with different num and den", {SK(S_CMPDIFF)}},
    {"4.NF.3", 4, DOM_FD, "Add and subtract like fractions and mixed numbers", {SK(S_ADDFRAC), SK(S_MIXED)}},
    {"4.NF.4", 4, DOM_FD, "Multiply a fraction by a whole number", {SK(S_FRACWHOLE)}},
    {"4.NF.5", 4, DOM_FD, "Tenths as hundredths: 3/10 = 30/100", {SK(S_TENTHS)}},
    {"4.NF.6", 4, DOM_FD, "Decimal notation for tenths and hundredths", {SK(S_DECIMAL), SK(S_MONEYDEC)}},
    {"4.NF.7", 4, DOM_FD, "Compare decimals to hundredths", {SK(S_CMPDEC)}},
    {"4.MD.1", 4, DOM_MD, "Sizes of units: km, m, cm; kg, g; lb, oz; l, ml; h, min", {SK(S_CONVERT)}},
    {"4.MD.2", 4, DOM_MD, "Word problems: distance, time, volume, mass, money",
     {SK(S_MEASWORD), SK(S_CHANGE), SK(S_TABLE)}},
    {"4.MD.3", 4, DOM_GE, "Area and perimeter formulas for rectangles", {SK(S_FORMULA)}},
    {"4.MD.4", 4, DOM_MD, "Line plots with fractions of a unit", {SK(S_LINEPLOT)}},
    {"4.MD.5", 4, DOM_GE, "Angles: a turn through n one-degree angles", {SK(S_ANGLES)}},
    {"4.MD.6", 4, DOM_GE, "Measure and sketch angles with a protractor", {SK(S_PROTRACTOR)}},
    {"4.MD.7", 4, DOM_GE, "Angle measures add; find unknown angles", {SK(S_ANGLEADD)}},
    {"4.G.1", 4, DOM_GE, "Points, lines, segments, rays, angles, parallel, perp.", {SK(S_LINES)}},
    {"4.G.2", 4, DOM_GE, "Classify figures by lines and angles; right triangles", {SK(S_CLASSIFY)}},
    {"4.G.3", 4, DOM_GE, "Lines of symmetry", {SK(S_SYMMETRY)}},
    /* grade 5 challenge track (optional) */
    {"5.NBT.3", 5, DOM_FD, "Read, write and compare decimals to thousandths", {SK(S_DEC1000)}},
    {"5.NBT.5", 5, DOM_NO, "Fluently multiply multi-digit whole numbers", {SK(S_MULBIG)}},
    {"5.NF.1", 5, DOM_FD, "Add and subtract fractions with unlike denominators", {SK(S_FRUNLIKE)}},
    {"5.MD.3", 5, DOM_MD, "Volume: unit cubes fill a solid figure", {SK(S_VOLUME)}},
    {"5.MD.5", 5, DOM_MD, "Volume of a rectangular prism: l x w x h", {SK(S_VOLUME)}},
    {"5.G.1", 5, DOM_GE, "The coordinate plane: points as (x, y)", {SK(S_COORD)}},
    {"5.G.2", 5, DOM_GE, "Graph points in the first quadrant to solve problems", {SK(S_COORD)}},
    {"5.OA.1", 5, DOM_AT, "Parentheses and brackets in numerical expressions", {SK(S_ORDEROPS)}},
    {"5.OA.2", 5, DOM_AT, "Write and interpret simple numerical expressions", {SK(S_EXPR)}},
    {"5.OA.3", 5, DOM_AT, "Two numerical patterns from two rules; their pairs", {SK(S_TWOPAT)}},
    {"5.NBT.1", 5, DOM_NO, "A digit is 10 times the place right, 1/10 the left", {SK(S_POW10)}},
    {"5.NBT.2", 5, DOM_NO, "Powers of 10; multiply and divide by powers of 10", {SK(S_POW10)}},
    {"5.NBT.4", 5, DOM_FD, "Round decimals to any place", {SK(S_ROUNDDEC)}},
    {"5.NBT.6", 5, DOM_NO, "Divide up to 4 digits by 2 digits", {SK(S_DIV2)}},
    {"5.NBT.7", 5, DOM_FD, "Add, subtract, multiply, divide decimals to 0.01", {SK(S_DECOPS)}},
    {"5.NF.2", 5, DOM_FD, "Word problems adding and subtracting unlike fractions", {SK(S_FRACWORD)}},
    {"5.NF.3", 5, DOM_FD, "A fraction as a division: a/b = a / b", {SK(S_FRACDIV)}},
    {"5.NF.4", 5, DOM_FD, "Multiply a fraction or whole number by a fraction", {SK(S_FRACMUL)}},
    {"5.NF.5", 5, DOM_FD, "Multiplying as scaling: bigger or smaller", {SK(S_SCALING)}},
    {"5.NF.6", 5, DOM_FD, "Real-world problems multiplying fractions", {SK(S_FRACMUL)}},
    {"5.NF.7", 5, DOM_FD, "Divide unit fractions by wholes, wholes by unit fractions", {SK(S_UNITDIV)}},
    {"5.MD.1", 5, DOM_MD, "Convert units within one system; multi-step problems", {SK(S_CONVERT5)}},
    {"5.MD.2", 5, DOM_MD, "Line plots of fractions; operations to solve problems", {SK(S_LINEPLOT5)}},
    {"5.MD.4", 5, DOM_MD, "Measure volume by counting unit cubes", {SK(S_VOLUME)}},
    {"5.G.3", 5, DOM_GE, "Attributes of a category belong to its subcategories", {SK(S_SHAPES5)}},
    {"5.G.4", 5, DOM_GE, "Classify two-dimensional figures in a hierarchy", {SK(S_SHAPES5)}},
};

/* visual_may_reveal: VR_MAY where reading or counting the model IS the skill (the clock, coins,
 * graphs, naming a shaded fraction, counting unit squares); VR_LEVEL0: the concept at difficulty 0
 * only (equal groups as dots, unit cubes); VR_NO: the question shows a model that cannot be
 * counted for the answer (labelled boxes, piles, dimensions); the full model comes after
 * the answer (feedback, worked example). Checked by tests/unit/test_mathquest.c. */
const mq_skill_t mq_skill[MQ_NSKILL] = {
    {"Equal groups", G_GARDEN, 3, VR_NO},       {"Sharing seeds", G_GARDEN, 3, VR_NO},     {"Garden stories", G_GARDEN, 3, VR_NO},
    {"Mystery number", G_GARDEN, 3, VR_NO},     {"Flip and split", G_GARDEN, 3, VR_NO},    {"Divide by thinking x", G_GARDEN, 3, VR_NO},
    {"Fact garden", G_GARDEN, 3, VR_NO},        {"Two-step stories", G_GARDEN, 3, VR_NO},  {"Number patterns", G_GARDEN, 3, VR_NO},
    {"Times as many", G_GARDEN, 4, VR_NO},      {"Leftover stories", G_GARDEN, 4, VR_NO},  {"Factor friends", G_GARDEN, 4, VR_NO},
    {"Pattern rules", G_GARDEN, 4, VR_NO},
    {"Hundreds tens ones", G_POST, 2, VR_MAY},   {"Bigger parcel", G_POST, 2, VR_MAY},       {"Round to 10 and 100", G_POST, 3, VR_MAY},
    {"Add and subtract", G_POST, 3, VR_NO},     {"Times tens", G_POST, 3, VR_NO},          {"Big number places", G_POST, 4, VR_MAY},
    {"Compare big numbers", G_POST, 4, VR_MAY},  {"Round big numbers", G_POST, 4, VR_MAY},   {"Big add and subtract", G_POST, 4, VR_NO},
    {"Multiply bigger", G_POST, 4, VR_NO},      {"Divide parcels", G_POST, 4, VR_NO},
    {"Name the slice", G_BAKERY, 3, VR_MAY},     {"Fractions on a line", G_BAKERY, 3, VR_MAY}, {"Same amount", G_BAKERY, 3, VR_NO},
    {"Wholes as fractions", G_BAKERY, 3, VR_MAY}, {"Compare slices", G_BAKERY, 3, VR_MAY},   {"Cut fair shares", G_BAKERY, 3, VR_MAY},
    {"Equivalent fractions", G_BAKERY, 4, VR_NO}, {"Compare any slices", G_BAKERY, 4, VR_NO}, {"Add and take slices", G_BAKERY, 4, VR_NO},
    {"Mixed numbers", G_BAKERY, 4, VR_NO},      {"Fraction x whole", G_BAKERY, 4, VR_NO},  {"Tenths, hundredths", G_BAKERY, 4, VR_NO},
    {"Decimals", G_BAKERY, 4, VR_MAY},           {"Compare decimals", G_BAKERY, 4, VR_MAY},
    {"Time to 5 minutes", G_CLOCK, 2, VR_MAY},   {"Time to the minute", G_CLOCK, 3, VR_MAY}, {"How long?", G_CLOCK, 3, VR_NO},
    {"Grams and liters", G_CLOCK, 3, VR_NO},    {"Unit sizes", G_CLOCK, 4, VR_NO},         {"Measuring stories", G_CLOCK, 4, VR_NO},
    {"Count coins", G_SHOP, 2, VR_MAY},          {"Making change", G_SHOP, 4, VR_NO},       {"Shop stories", G_SHOP, 3, VR_NO},
    {"Money decimals", G_SHOP, 4, VR_NO},
    {"Count squares", G_ART, 3, VR_MAY},         {"Area by multiplying", G_ART, 3, VR_NO},  {"Split the rectangle", G_ART, 3, VR_NO},
    {"L-shaped rooms", G_ART, 3, VR_LEVEL0},        {"Around the edge", G_ART, 3, VR_NO},      {"Shape families", G_ART, 3, VR_MAY},
    {"Area & perimeter rules", G_ART, 4, VR_NO}, {"Angles in degrees", G_ART, 4, VR_MAY},  {"Protractor", G_ART, 4, VR_MAY},
    {"Adding angles", G_ART, 4, VR_NO},         {"Lines and rays", G_ART, 4, VR_MAY},       {"Sort shapes", G_ART, 4, VR_MAY},
    {"Mirror lines", G_ART, 4, VR_MAY},
    {"Picture graphs", G_FAIR, 2, VR_MAY},       {"Scaled graphs", G_FAIR, 3, VR_MAY},       {"Measure and plot", G_FAIR, 3, VR_MAY},
    {"Fraction line plots", G_FAIR, 4, VR_MAY},  {"Read a table", G_FAIR, 4, VR_MAY},
    {"Mega multiply", G_SKY, 5, VR_NO},         {"Unlike slices", G_STAR, 5, VR_NO},        {"Thousandths", G_SKY, 5, VR_MAY},
    {"Box volume", G_STAR, 5, VR_LEVEL0},           {"Treasure map", G_SKY, 5, VR_MAY},
    {"Powers of ten", G_SKY, 5, VR_NO},         {"Round decimals", G_SKY, 5, VR_NO},       {"Decimal sums", G_SKY, 5, VR_NO},
    {"Two-digit sharing", G_SKY, 5, VR_NO},     {"Brackets first", G_SKY, 5, VR_NO},       {"Expressions", G_SKY, 5, VR_NO},
    {"Two patterns", G_SKY, 5, VR_NO},          {"Unlike stories", G_STAR, 5, VR_NO},      {"Fractions as sharing", G_STAR, 5, VR_NO},
    {"Fraction of a fraction", G_STAR, 5, VR_NO}, {"Scaling", G_STAR, 5, VR_NO},           {"Dividing unit fractions", G_STAR, 5, VR_NO},
    {"Converting units", G_STAR, 5, VR_NO},     {"Fraction plots", G_STAR, 5, VR_NO},      {"Shape families tree", G_STAR, 5, VR_MAY},
};
_Static_assert(S_COUNT == MQ_NSKILL, "skill table size");

/* Prerequisites: the skills a skill builds on (S_* + 1). A struggle with a skill pulls its
 * prerequisites into the interleaved review (mq_learn.c). */
#define P(a) (uint8_t)((a) + 1)
const uint8_t mq_prereq[MQ_NSKILL][2] = {
    [S_SHARE] = {P(S_GROUPS)}, [S_MDWORD] = {P(S_GROUPS), P(S_SHARE)}, [S_UNKNOWN] = {P(S_GROUPS), P(S_DIVFACT)},
    [S_PROPS] = {P(S_GROUPS)}, [S_DIVFACT] = {P(S_GROUPS), P(S_SHARE)}, [S_TWOSTEP] = {P(S_MDWORD), P(S_ADDSUB)},
    [S_PATTERN] = {P(S_ADDSUB)}, [S_TIMES_AS] = {P(S_GROUPS)}, [S_REMWORD] = {P(S_SHARE), P(S_DIVFACT)},
    [S_FACTORS] = {P(S_GROUPS), P(S_DIVFACT)}, [S_RULE] = {P(S_PATTERN)},
    [S_CMP3] = {P(S_PV3)}, [S_ROUND10] = {P(S_PV3)}, [S_ADDSUB] = {P(S_PV3)}, [S_MUL10] = {P(S_GROUPS), P(S_PV3)},
    [S_PVBIG] = {P(S_PV3)}, [S_CMPBIG] = {P(S_CMP3), P(S_PVBIG)}, [S_ROUNDBIG] = {P(S_ROUND10), P(S_PVBIG)},
    [S_ADDSUBBIG] = {P(S_ADDSUB)}, [S_MULTI] = {P(S_MUL10), P(S_PROPS)}, [S_DIVIDE] = {P(S_DIVFACT), P(S_REMWORD)},
    [S_FRACLINE] = {P(S_UNITFRAC)}, [S_EQUIV] = {P(S_UNITFRAC), P(S_FRACLINE)}, [S_WHOLES] = {P(S_UNITFRAC)},
    [S_CMPFRAC] = {P(S_UNITFRAC)}, [S_PARTITION] = {P(S_UNITFRAC)}, [S_EQUIVMUL] = {P(S_EQUIV), P(S_GROUPS)},
    [S_CMPDIFF] = {P(S_EQUIVMUL), P(S_CMPFRAC)}, [S_ADDFRAC] = {P(S_EQUIV), P(S_UNITFRAC)},
    [S_MIXED] = {P(S_WHOLES), P(S_ADDFRAC)}, [S_FRACWHOLE] = {P(S_ADDFRAC), P(S_GROUPS)},
    [S_TENTHS] = {P(S_EQUIVMUL)}, [S_DECIMAL] = {P(S_TENTHS)}, [S_CMPDEC] = {P(S_DECIMAL)},
    [S_TIME1] = {P(S_TIME5)}, [S_ELAPSED] = {P(S_TIME1), P(S_ADDSUB)}, [S_CONVERT] = {P(S_MUL10)},
    [S_MEASWORD] = {P(S_ADDSUB)}, [S_CHANGE] = {P(S_COINS), P(S_ADDSUB)}, [S_SHOPWORD] = {P(S_ADDSUB), P(S_GROUPS)},
    [S_MONEYDEC] = {P(S_COINS), P(S_DECIMAL)}, [S_AREAMUL] = {P(S_AREACOUNT), P(S_GROUPS)},
    [S_AREASPLIT] = {P(S_AREAMUL), P(S_PROPS)}, [S_AREAL] = {P(S_AREAMUL)}, [S_PERIM] = {P(S_AREACOUNT), P(S_ADDSUB)},
    [S_FORMULA] = {P(S_AREAMUL), P(S_PERIM)}, [S_PROTRACTOR] = {P(S_ANGLES)}, [S_ANGLEADD] = {P(S_PROTRACTOR)},
    [S_CLASSIFY] = {P(S_SHAPES), P(S_LINES)}, [S_SYMMETRY] = {P(S_SHAPES)}, [S_SCALED] = {P(S_GRAPH1), P(S_GROUPS)},
    [S_LINEPLOT] = {P(S_RULER), P(S_ADDFRAC)}, [S_TABLE] = {P(S_ADDSUB)},
    [S_MULBIG] = {P(S_MULTI)}, [S_FRUNLIKE] = {P(S_EQUIVMUL), P(S_ADDFRAC)}, [S_DEC1000] = {P(S_DECIMAL)},
    [S_VOLUME] = {P(S_AREAMUL)}, [S_COORD] = {P(S_FRACLINE)},
    [S_POW10] = {P(S_PVBIG), P(S_DECIMAL)}, [S_ROUNDDEC] = {P(S_ROUNDBIG), P(S_DECIMAL)},
    [S_DECOPS] = {P(S_DECIMAL), P(S_ADDSUBBIG)}, [S_DIV2] = {P(S_DIVIDE)}, [S_ORDEROPS] = {P(S_TWOSTEP), P(S_PROPS)},
    [S_EXPR] = {P(S_PROPS), P(S_TIMES_AS)}, [S_TWOPAT] = {P(S_RULE)}, [S_FRACWORD] = {P(S_FRUNLIKE), P(S_ADDFRAC)},
    [S_FRACDIV] = {P(S_SHARE), P(S_UNITFRAC)}, [S_FRACMUL] = {P(S_FRACWHOLE), P(S_AREAMUL)},
    [S_SCALING] = {P(S_FRACWHOLE), P(S_CMPDIFF)}, [S_UNITDIV] = {P(S_FRACDIV), P(S_SHARE)},
    [S_CONVERT5] = {P(S_CONVERT), P(S_DECIMAL)}, [S_LINEPLOT5] = {P(S_LINEPLOT), P(S_ADDFRAC)},
    [S_SHAPES5] = {P(S_CLASSIFY), P(S_SHAPES)},
};
#undef P

const uint8_t mq_levels[MQ_NWORLD][MQ_MAXLV] = {
    {SK(S_GROUPS), SK(S_SHARE), SK(S_FACTS), SK(S_UNKNOWN), SK(S_DIVFACT), SK(S_PROPS), SK(S_MDWORD),
     SK(S_PATTERN), SK(S_TWOSTEP), SK(S_TIMES_AS), SK(S_FACTORS), SK(S_REMWORD), SK(S_RULE)},
    {SK(S_PV3), SK(S_CMP3), SK(S_ADDSUB), SK(S_ROUND10), SK(S_MUL10), SK(S_PVBIG), SK(S_CMPBIG),
     SK(S_ROUNDBIG), SK(S_ADDSUBBIG), SK(S_MULTI), SK(S_DIVIDE)},
    {SK(S_UNITFRAC), SK(S_PARTITION), SK(S_FRACLINE), SK(S_EQUIV), SK(S_WHOLES), SK(S_CMPFRAC),
     SK(S_EQUIVMUL), SK(S_CMPDIFF), SK(S_ADDFRAC), SK(S_MIXED), SK(S_FRACWHOLE), SK(S_TENTHS), SK(S_DECIMAL),
     SK(S_CMPDEC)},
    {SK(S_TIME5), SK(S_TIME1), SK(S_ELAPSED), SK(S_MASSVOL), SK(S_CONVERT), SK(S_MEASWORD)},
    {SK(S_COINS), SK(S_SHOPWORD), SK(S_CHANGE), SK(S_MONEYDEC)},
    {SK(S_AREACOUNT), SK(S_AREAMUL), SK(S_PERIM), SK(S_AREASPLIT), SK(S_AREAL), SK(S_SHAPES), SK(S_FORMULA),
     SK(S_LINES), SK(S_ANGLES), SK(S_PROTRACTOR), SK(S_ANGLEADD), SK(S_CLASSIFY), SK(S_SYMMETRY)},
    {SK(S_GRAPH1), SK(S_SCALED), SK(S_TABLE), SK(S_RULER), SK(S_LINEPLOT)},
    {SK(S_POW10), SK(S_DEC1000), SK(S_ROUNDDEC), SK(S_DECOPS), SK(S_MULBIG), SK(S_DIV2), SK(S_ORDEROPS),
     SK(S_EXPR), SK(S_TWOPAT), SK(S_COORD)},
    {SK(S_FRUNLIKE), SK(S_FRACWORD), SK(S_FRACDIV), SK(S_FRACMUL), SK(S_SCALING), SK(S_UNITDIV), SK(S_CONVERT5),
     SK(S_LINEPLOT5), SK(S_VOLUME), SK(S_SHAPES5)},
};

const char mq_world_name[MQ_NWORLD][16] = {"Garden", "Post Office", "Bakery", "Clock Tower",
                                           "Shop", "Art Studio", "Fair", "Sky Castle", "Star Harbor"};
const char mq_dom_name[MQ_NDOM][28] = {"Numbers and operations", "Algebraic thinking", "Fractions and decimals",
                                       "Geometry", "Measurement and data"};

const char mq_mc_name[MC_COUNT][40] = {
    "",
    "A little slip",
    "Added instead of multiplied",
    "A neighbour sneaked in",
    "Counted just one group",
    "Mixed up the sharing",
    "The ten did not get carried",
    "Took the small digit from the big one",
    "Borrowing across a zero",
    "A digit slid to another place",
    "Always rounded down",
    "Looked at the wrong digit to round",
    "Lost or added a zero",
    "Bigger bottom number, bigger slice?",
    "Top and bottom swapped",
    "Added the bottom numbers too",
    "Counted the tick marks, not the jumps",
    "The parts were not equal",
    "A longer decimal is not bigger",
    "The decimal point moved",
    "Area and perimeter mixed up",
    "Only two sides were added",
    "Hour and minute hands swapped",
    "Minute marks counted by ones",
    "An hour is 60 minutes, not 100",
    "Forgot the leftovers",
    "The leftover is too big",
    "The leftovers need one more",
    "Converted the wrong way",
    "Read the other protractor scale",
    "Angle size mix-up",
    "Judged the shape by how it looks",
    "The graph key was skipped",
    "Only one step was done",
    "Used the other operation",
    "A factor was missed",
    "Odd does not always mean prime",
    "Counted coins, not their value",
    "Whole and part mixed up",
    "Multiplied the bottom number too",
    "Tenths and hundredths mixed up",
    "x and y swapped",
    "Only the floor was counted",
    "Added instead of times as many",
    "The pattern step is off",
    "Added the same number to top and bottom",
    "A partial product went missing",
    "Read the wrong bar or row",
    "That line does not fold evenly",
    "Lines, rays and segments mixed up",
    "So close!",
    "Did the brackets wait their turn",
    "Times always makes bigger",
    "Flipped the sharing around",
};

const char mq_mc_help[MC_COUNT][2][46] = {
    {"", ""},
    {"Everyone slips sometimes.", "Let's look at it together."},
    {"Times means equal groups.", "Count the groups, then how many in each."},
    {"That one belongs to the one next door.", "Count one more or one fewer group."},
    {"That is how many in one group.", "Now count every group."},
    {"Share means how many in each group.", "Think: groups x each = total."},
    {"When a column makes 10 or more,", "carry 1 to the next column."},
    {"When the top digit is smaller,", "regroup: borrow 1 from the next place."},
    {"A zero cannot lend: go one place further", "and regroup step by step."},
    {"Each place is 10 times the one to its right.", "Line the digits up by place."},
    {"Look at the digit to the right:", "5 or more rounds up."},
    {"Find the place you round to, then", "look only at the digit just right of it."},
    {"Count the zeros carefully:", "3 x 40 is 3 x 4 tens = 12 tens."},
    {"More pieces means smaller pieces!", "1/8 is smaller than 1/4."},
    {"Bottom = how many equal parts.", "Top = how many parts we have."},
    {"The pieces stay the same size:", "add only the top numbers."},
    {"Count the jumps from 0,", "not the marks."},
    {"Fraction parts must be equal", "in size."},
    {"Compare tenths first: 0.5 = 0.50", "and 0.50 is more than 0.45."},
    {"Tenths are 1 place right of the point,", "hundredths are 2 places."},
    {"Area = squares inside.", "Perimeter = the distance around the edge."},
    {"Perimeter goes all the way around:", "add all four sides."},
    {"The short hand shows the hour,", "the long hand shows the minutes."},
    {"Each number on the clock", "is 5 minutes."},
    {"Count on to the next hour first,", "then add the minutes left."},
    {"Something is left over:", "write it after R."},
    {"The leftover must be smaller", "than the number you divide by."},
    {"Leftovers need a place too,", "so we need one more."},
    {"Big units hold many small units:", "to small units, multiply."},
    {"Start from the 0 on the scale", "where the angle starts."},
    {"A right angle is 90 degrees.", "Smaller is acute, bigger is obtuse."},
    {"Count the sides and check the angles,", "not the turn of the shape."},
    {"Each picture can stand for more than 1.", "Check the key!"},
    {"This story has two steps.", "Do the first, then use it in the second."},
    {"Read the story again:", "is it joining, taking or sharing?"},
    {"Factor pairs come in partners.", "Check every number from 1 up."},
    {"9 and 15 are odd, and still make rows:", "3 rows of 3, 3 rows of 5."},
    {"A quarter is 25 cents, a dime 10,", "a nickel 5, a penny 1."},
    {"Wholes first: each whole", "is a full set of pieces."},
    {"The pieces stay the same size:", "multiply only the top number."},
    {"1 tenth = 10 hundredths.", "0.3 is the same as 0.30."},
    {"Walk along x first,", "then climb up y."},
    {"Volume fills the whole box:", "length x width x height."},
    {"'Times as many' means multiply,", "not add."},
    {"Find how much each step grows", "and keep that rule."},
    {"Multiply top and bottom", "by the same number."},
    {"Multiply each part, then add", "all the partial products."},
    {"Put your finger on the bar or row", "and read across."},
    {"A mirror line folds the shape", "into two matching halves."},
    {"A segment has 2 ends, a ray 1 end,", "a line goes on both ways."},
    {"You are very close!", "Check the last step again."},
    {"Brackets go first, then times and divide,", "then add and take away."},
    {"Times a number less than 1", "makes it smaller!"},
    {"Sharing 1/3 by 2 makes smaller pieces;", "how many 1/3s fit in 2 is more."},
};

/* number of levels of world w (before its boss) */
int mq_world_levels(int w)
{
    int n = 0;
    while (n < MQ_MAXLV && mq_levels[w][n])
        n++;
    return n;
}

bool mq_std_passed(const mq_profile_t *p, int s)
{
    int any = 0;
    for (int k = 0; k < 4; k++) {
        int sk = mq_std[s].skills[k];
        if (!sk)
            continue;
        any++;
        if (!(p->sk[sk - 1].flags & SK_PASSED))
            return false;
    }
    return any > 0;
}

/* Standards of a domain passed / total (grades 2-4; the challenge track does
 * not count). Returns the percentage. */
int mq_dom_progress(const mq_profile_t *p, int dom, int *passed, int *total)
{
    int n = 0, k = 0;
    for (int s = 0; s < MQ_NSTD; s++)
        if (mq_std[s].domain == dom && mq_std[s].grade <= 4) {
            n++;
            k += mq_std_passed(p, s);
        }
    if (passed)
        *passed = k;
    if (total)
        *total = n;
    return n ? k * 100 / n : 100;
}

/* The game is finished when every standard of grades 2-4 is passed (that
 * includes 3.OA.7, whose skill is passed only when every required fact is
 * fluent: mq_main.c). */
bool mq_quest_complete(const mq_profile_t *p)
{
    for (int s = 0; s < MQ_NSTD; s++)
        if (mq_std[s].grade <= 4 && !mq_std_passed(p, s))
            return false;
    int total, fl = mq_facts_fluent(p, &total);
    return fl == total;
}

/* Ready for grade 5: some grade-5 skill whose grade-4 prerequisites are all mastered. The game
 * moves her on as soon as she is ready (Sky Castle opens), not after finishing everything. */
bool mq_grade5_ready(const mq_profile_t *p)
{
    for (int s = 0; s < MQ_NSKILL; s++) {
        if (mq_skill[s].grade != 5)
            continue;
        int n = 0, ok = 0, g4 = 0;
        for (int k = 0; k < 2; k++)
            if (mq_prereq[s][k]) {
                int r = mq_prereq[s][k] - 1;
                if (mq_skill[r].grade == 5)
                    continue;
                n++, ok += (p->sk[r].flags & SK_PASSED) != 0, g4 += mq_skill[r].grade == 4;
            }
        if (n && ok == n && g4)
            return true;
    }
    return false;
}

/* The grade she is working at (the grown-ups' screen): the highest grade of a skill she has
 * played a few times, at least 3. */
int mq_working_grade(const mq_profile_t *p)
{
    int g = 3;
    for (int s = 0; s < MQ_NSKILL; s++)
        if (p->sk[s].tries >= 3 && mq_skill[s].grade > g)
            g = mq_skill[s].grade;
    return g;
}

/* Every stop can be played from the start (nothing is locked; the engine picks the maths). */
bool mq_skill_unlocked(const mq_profile_t *p, int world, int level)
{
    (void)p;
    return world >= 0 && world < MQ_NWORLD && level >= 0 && level < mq_world_levels(world);
}

/* The coverage check of the unit test (also `mq coverage` on the console):
 * every standard maps to at least one skill, every skill is mapped by a
 * standard and is a level of its world. Returns the number of problems and
 * describes the first one in err. */
int mq_coverage_check(char *err, size_t n, const modx_api_t *api)
{
    int bad = 0;
    err[0] = 0;
    for (int s = 0; s < MQ_NSTD; s++) {
        int k = 0;
        for (int j = 0; j < 4; j++)
            k += mq_std[s].skills[j] != 0;
        if (!k || !mq_std[s].desc[0]) {
            if (!bad++)
                api->snprintf(err, n, "standard %s is not mapped", mq_std[s].id);
        }
    }
    for (int sk = 0; sk < MQ_NSKILL; sk++) {
        bool mapped = false, onlevel = false;
        for (int s = 0; s < MQ_NSTD; s++)
            for (int j = 0; j < 4; j++)
                mapped |= mq_std[s].skills[j] == sk + 1;
        int w = mq_skill[sk].world;
        for (int l = 0; l < MQ_MAXLV; l++)
            onlevel |= mq_levels[w][l] == sk + 1;
        if (!mapped || !onlevel) {
            if (!bad++)
                api->snprintf(err, n, "skill %s is %s", mq_skill[sk].name, mapped ? "on no level" : "not mapped");
        }
    }
    return bad;
}
