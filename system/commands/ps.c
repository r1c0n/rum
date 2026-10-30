#include <rum/tools.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: ps\n"); return 1; }
    struct rum_process_entry entries[RUM_PROCESS_LIST_LIMIT]; uint32_t total;
    rum_result_t count = rum_process_list(entries, RUM_PROCESS_LIST_LIMIT, &total);
    if (count < 0) { tool_error("ps", count); return 1; }
    tool_print("PID  PARENT  STATE  NAME\n");
    for (int i = 0; i < count; ++i) {
        const struct rum_process_entry *p = &entries[i];
        tool_number(p->process_id); tool_print("  "); tool_number(p->parent_id); tool_print("  ");
        tool_print(p->flags & RUM_PROCESS_CANCELLING ? "cancelling" :
            p->state == RUM_PROCESS_RUNNABLE ? "ready" : p->state == RUM_PROCESS_RUNNING ? "running" :
            p->state == RUM_PROCESS_WAITING ? "waiting" : "exited");
        tool_print("  "); tool_print(p->name); tool_print("\n");
    }
    return 0;
}
