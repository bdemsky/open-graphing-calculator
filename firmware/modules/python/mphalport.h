/* MicroPython HAL of the calculator's Python module: time through the modx API
 * (modules/python/py_entry.c), no pins, no stdin. */
#ifndef PY_MPHALPORT_H
#define PY_MPHALPORT_H
#define mp_hal_pin_obj_t void *
static inline void mp_hal_set_interrupt_char(int c) { (void)c; }
#endif
