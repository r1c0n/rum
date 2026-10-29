#ifndef RUM_SESSION_H
#define RUM_SESSION_H
#include <stdbool.h>
#include <stdint.h>

struct console_session;
/* Embedded in a stable process record. Session/root/owner/return references
   must be released before this record can be reused. No public user pointer. */
struct console_member {
    struct console_session *session;
    struct console_member *restore;
    uint32_t id, references;
    bool live, cancelled, shell, command, foreground_launch;
};
struct console_statistics {
    uint32_t sessions, closing, members, session_references, process_references;
    uint32_t owner, shell;
};

/* NULL parent means a trusted kernel producer. Call can_launch again with IF
   clear immediately before publication; attach to the stable published record
   in that same update. All operations are bounded and allocation-free. */
bool console_can_launch(const struct console_member *parent, bool foreground);
bool console_attach(struct console_member *, uint32_t id, struct console_member *parent, bool foreground);
bool console_mark_shell(struct console_member *);
bool console_is_shell(const struct console_member *);
bool console_owns_input(const struct console_member *);
bool console_grant_command(struct console_member *shell, struct console_member *child);
struct console_member *console_command_shell(const struct console_member *);
bool console_same_session(const struct console_member *, const struct console_member *);
bool console_is_root(const struct console_member *);
uint32_t console_cancellation_target(void); /* IRQ-safe; shell receives Ctrl+C as input. */
void console_cancel_member(struct console_member *);
/* Restore a referenced previous owner or close a root session. True requests
   discarding input from a terminated interactive shell, before its restart. */
bool console_member_exit(struct console_member *);
bool console_detach(struct console_member *); /* Exited, unreferenced members only. */
bool console_request_exit(struct console_member *command, int32_t status);
bool console_exit_requested(const struct console_member *shell, int32_t *status);
struct console_statistics console_stats(void);
#endif
