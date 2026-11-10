/* A headless platform adapter for link diagnostics, not a graphics backend. */
#include <rum/user.h>
#include "doomgeneric.h"
void DG_Init(void) { }
void DG_DrawFrame(void) { }
void DG_SetWindowTitle(const char *title) { (void)title; }
int DG_GetKey(int *pressed, unsigned char *key) { (void)pressed; (void)key; return 0; }
void DG_SleepMs(uint32_t milliseconds) { (void)rum_sleep(milliseconds); }
uint32_t DG_GetTicksMs(void)
{
    uint64_t milliseconds = 0; (void)rum_monotonic_ms(&milliseconds); return (uint32_t)milliseconds;
}
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    doomgeneric_Tick();
    return 0;
}
