#include <rum/power.h>
#include <rum/task.h>
#include <rum/timer.h>

enum fs_error system_power(enum power_action action)
{
    if (action != POWER_SHUTDOWN && action != POWER_REBOOT) return FS_INVALID;
    if (task_current_is_process() || !task_blocking_allowed()) return FS_ACCESS;
    /* Session teardown has cancelled/adopted its children. Let their ordinary
       exit paths close descriptors before the final filesystem flush. */
    uint32_t started = timer_ticks();
    for (;;) {
        (void)task_reap();
        uint32_t total;
        (void)task_process_list(NULL, 0, &total);
        if (!total) break;
        if ((uint32_t)(timer_ticks() - started) >= 5u * TIMER_HZ) return FS_BUSY;
        (void)task_yield();
    }
    fs_reference root;
    enum fs_error error = fs_open(NULL, "/", FS_READ, &root);
    if (error == FS_OK) {
        error = fs_flush(root);
        enum fs_error closed = fs_close(root);
        if (error == FS_OK) error = closed;
    } else if (error == FS_UNAVAILABLE) error = FS_OK; /* Diskless recovery. */
    if (error != FS_OK) return error;
    power_machine(action);
}
