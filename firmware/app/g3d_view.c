/*
 * 3D graphing camera (see g3d.h): world -> eye rotation, weak perspective
 * projection to screen pixels, and the sine/cosine it is built from.
 *
 * Camera model. The view box [-1,1]^3 is turned about the world z axis by the
 * azimuth a and then tilted towards the viewer by the elevation e:
 *
 *   right   = ( cos a,       -sin a,        0     )
 *   up      = ( sin a sin e,  cos a sin e,  cos e )
 *   towards = (-sin a cos e, -cos a cos e,  sin e )
 *
 * (rows of the rotation matrix; an orthonormal right-handed basis, right x up
 * = towards). With a = e = 0 the viewer looks along +y: x points right and z
 * up; a positive elevation looks down onto the x-y plane, e = 90 is the top
 * view. Projection: eye coordinates (X, Y, Z) = M w, perspective factor
 * f = E / (E - Z) with the eye at distance E = G3_EYE, pixel
 * (cx + s f X, cy - s f Y) and depth d = 1 / (E - Z). d is an affine function
 * of the screen position over any plane, so the rasterizer interpolates it
 * linearly; larger d is nearer.
 *
 * Determinism: the sine and cosine are computed here with a polynomial
 * instead of libm, which differs between the native simulator and the
 * WebAssembly build in the last bit, so both builds draw identical pixels
 * (the UI tests compare their screenshots). Compiled with floating-point
 * contraction off for the same reason (no fused multiply-add on one side only).
 */
#ifdef __clang__
#pragma clang fp contract(off)
#endif
#include <math.h>
#include "g3d.h"

/* sin / cos of |x| <= pi/4 by their Taylor series, nested (Horner) form; the
 * first omitted terms (x^19/19!, x^18/18!) are below 3e-17 there. */
static double sin_poly(double x)
{
    double x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156 * (1 - x2 / 210 * (1 - x2 / 272))))))));
}
static double cos_poly(double x)
{
    double x2 = x * x;
    return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90 * (1 - x2 / 132 * (1 - x2 / 182 * (1 - x2 / 240)))))));
}

/* Sine of an angle in degrees: reduce to [0, 360) with fmod (exact), pick the
 * nearest multiple q of 90 degrees and evaluate sin or cos of the remainder
 * (|r| <= 45 degrees) with the quadrant's sign. Exact at multiples of 90. */
double g3_sin_deg(double deg)
{
    double d = fmod(deg, 360.0);
    if (d < 0)
        d += 360.0;
    int q = (int)floor(d / 90.0 + 0.5);
    double r = (d - q * 90.0) * (M_PI / 180.0);
    switch (q & 3) {
    case 0: return sin_poly(r);
    case 1: return cos_poly(r);
    case 2: return -sin_poly(r);
    default: return -cos_poly(r);
    }
}

double g3_cos_deg(double deg) { return g3_sin_deg(deg + 90.0); }

/* Fill the rotation matrix (rows right, up, towards; see the file comment)
 * and the pixel mapping. */
void g3_view_setup(g3_view_t *v, double azim_deg, double elev_deg, float cx, float cy, float scale)
{
    float sa = (float)g3_sin_deg(azim_deg), ca = (float)g3_cos_deg(azim_deg);
    float se = (float)g3_sin_deg(elev_deg), ce = (float)g3_cos_deg(elev_deg);
    v->m[0][0] = ca, v->m[0][1] = -sa, v->m[0][2] = 0;
    v->m[1][0] = sa * se, v->m[1][1] = ca * se, v->m[1][2] = ce;
    v->m[2][0] = -sa * ce, v->m[2][1] = -ca * ce, v->m[2][2] = se;
    v->cx = cx, v->cy = cy, v->scale = scale;
}

/* Eye coordinates (right, up, towards the viewer) of a normalised point. */
void g3_view_eye(const g3_view_t *v, const g3v_t *w, float e[3])
{
    for (int r = 0; r < 3; r++)
        e[r] = v->m[r][0] * w->x + v->m[r][1] * w->y + v->m[r][2] * w->z;
}

/* Project one point (see the file comment). Undefined points (x = NAN) get
 * d = NAN, which the renderer tests for. Callers keep points within G3_FAR of
 * the box, so E - Z > 0 always. */
void g3_project(const g3_view_t *v, const g3v_t *w, g3p_t *p)
{
    if (isnan(w->x)) {
        p->sx = p->sy = 0;
        p->d = NAN;
        return;
    }
    float e[3];
    g3_view_eye(v, w, e);
    float d = 1.0f / (G3_EYE - e[2]);
    float f = G3_EYE * d * v->scale;
    p->sx = v->cx + f * e[0];
    p->sy = v->cy - f * e[1];
    p->d = d;
}

/* Project n points (the cached mesh nodes; rotation never re-evaluates). */
void g3_project_all(const g3_view_t *v, const g3v_t *w, g3p_t *p, int n)
{
    for (int i = 0; i < n; i++)
        g3_project(v, &w[i], &p[i]);
}
