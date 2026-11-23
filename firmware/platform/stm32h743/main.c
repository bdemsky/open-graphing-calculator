/* Firmware entry point (called from Reset_Handler in startup.c).
 * hal_init() (hal_stm32.c) brings up clocks, memories, display and inputs;
 * calc_main() (core/calc.c) is the platform-independent application loop,
 * shared with the desktop and web simulators. */
#include "calc.h"
#include "hal.h"
#include "platform.h"

int main(void)
{
    hal_init();
    calc_main();
    /* calc_main only returns on a console 'quit', which means nothing on
     * hardware: restart. */
    NVIC_SystemReset();
    return 0;
}
