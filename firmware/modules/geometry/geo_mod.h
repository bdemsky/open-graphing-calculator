/*
 * geo_mod.h -- included last by the source files of the geometry module.
 * The core's functions are reached through tables, never by name: an image
 * that runs in place from the QSPI has no GOT and its link fails on any
 * undefined symbol (app/modx.h). The usual names below expand to calls
 * through geo_core_t (app/geo_core.h) and modx_api_t, so the code reads like
 * core code. The simulators and the core tier link the same sources into
 * the firmware and take the same path, so the tables are what every test
 * exercises.
 *
 * fabs, isfinite, isnan, isinf, NAN and INFINITY stay the compiler's
 * (inline instructions and constants on every target).
 */
#ifndef GEO_MOD_H
#define GEO_MOD_H
#include "geo.h"

#define GI (GEO_RW->core)
#define GA (GEO_RW->api)

#undef snprintf
#undef strcmp
#define snprintf GA->snprintf
#define strcmp GA->strcmp
#define con_printf GA->con_printf
#define con_error GA->con_error

#define sin GI->sin
#define cos GI->cos
#define tan GI->tan
#define acos GI->acos
#define atan2 GI->atan2
#define sqrt GI->sqrt
#define hypot GI->hypot
#define fmod GI->fmod
#define pow GI->pow
#define log10 GI->log10
#define floor GI->floor
#define ceil GI->ceil
#define round GI->round
#define fmin GI->fmin
#define fmax GI->fmax
#define lround GI->lround
#define strlen GI->strlen
#define strstr GI->strstr
#define atof GI->atof

#define pw_u8 GI->pw_u8
#define pw_f64 GI->pw_f64
#define pw_str GI->pw_str
#define pr_u8 GI->pr_u8
#define pr_f64 GI->pr_f64
#define pr_str GI->pr_str

#define gfx_pixel GI->gfx_pixel
#define gfx_hline GI->gfx_hline
#define gfx_vline GI->gfx_vline
#define gfx_line GI->gfx_line
#define gfx_fill_rect GI->gfx_fill_rect
#define gfx_text GI->gfx_text
#define gfx_text_width GI->gfx_text_width
#define gfx_set_clip GI->gfx_set_clip
#define gfx_reset_clip GI->gfx_reset_clip
#undef font_small
#undef font_medium
#define font_small (*GI->font_small)
#define font_medium (*GI->font_medium)

#define funcs_compile GI->funcs_compile
#define func_eval GI->func_eval
#define graph_invalidate GI->graph_invalidate
#define gov_window GI->gov_window
#define gov_line GI->gov_line
#define gov_mark GI->gov_mark

#undef fabs
#define fabs __builtin_fabs /* inline (an image is freestanding: no builtins by name) */
#define iabs(v) ((v) < 0 ? -(v) : (v))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif
