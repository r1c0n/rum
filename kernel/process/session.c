#include <rum/session.h>
#include <rum/cpu.h>
#include <rum/process_limits.h>

/* One session per possible process is sufficient even while a closed shell's
   children finish cleanup and its successor starts in another session. */
struct console_session {
    uint32_t references;
    bool closing, exit_requested;
    int32_t exit_status;
    struct console_member *root, *owner;
    struct console_member *members[RUM_PROCESS_LIMIT];
};
static struct console_session sessions[RUM_PROCESS_LIMIT];
static struct console_session *active;

static struct console_session *available(void)
{
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i)
        if (!sessions[i].references) return &sessions[i];
    return 0;
}
static bool owns(const struct console_member *member)
{
    return member && member->live && !member->cancelled && member->session &&
        member->session == active && !active->closing && active->owner == member;
}
static void owner(struct console_session *session, struct console_member *next)
{
    if (session->owner) --session->owner->references;
    session->owner = next;
    if (next) ++next->references;
}
static void release_return(struct console_member *member)
{
    if (member->restore) --member->restore->references;
    member->restore = 0;
}
static struct console_member *previous(struct console_member *member)
{
    struct console_member *candidate = member->restore;
    for (unsigned i = 0; candidate && i < RUM_PROCESS_LIMIT; ++i) {
        if (candidate->live && !candidate->cancelled && candidate->session == member->session) return candidate;
        candidate = candidate->restore;
    }
    return 0;
}
bool console_can_launch(const struct console_member *parent, bool foreground)
{
    uint32_t saved = cpu_interrupt_save();
    bool valid = parent ? parent->live && !parent->cancelled &&
        (!parent->session || !parent->session->closing) && (!foreground || owns(parent)) :
        !foreground || (!active && available());
    if (valid && parent && parent->session) {
        unsigned i = 0;
        while (i < RUM_PROCESS_LIMIT && parent->session->members[i]) ++i;
        valid = i != RUM_PROCESS_LIMIT;
    }
    cpu_interrupt_restore(saved);
    return valid;
}
bool console_attach(struct console_member *member, uint32_t id, struct console_member *parent, bool foreground)
{
    if (!member || !id) return false;
    uint32_t saved = cpu_interrupt_save();
    if (member->live || member->session || member->references || !console_can_launch(parent, foreground)) {
        cpu_interrupt_restore(saved); return false;
    }
    struct console_session *session = parent ? parent->session : foreground ? available() : 0;
    unsigned slot = 0;
    if (session) {
        while (slot < RUM_PROCESS_LIMIT && session->members[slot]) ++slot;
        if (slot == RUM_PROCESS_LIMIT) { cpu_interrupt_restore(saved); return false; }
    }
    *member = (struct console_member){ .session = session, .id = id, .live = true, .foreground_launch = foreground };
    if (session) {
        session->members[slot] = member; ++session->references;
        if (foreground && parent) { member->restore = parent; ++parent->references; }
        if (foreground && !parent) {
            active = session; ++session->references; /* Active-console reference. */
            session->root = member; ++member->references;
        }
        if (foreground) owner(session, member);
    }
    cpu_interrupt_restore(saved);
    return true;
}
bool console_mark_shell(struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save();
    bool valid = owns(member) && member->session->root == member && !member->shell;
    if (valid) member->shell = true;
    cpu_interrupt_restore(saved); return valid;
}
bool console_is_shell(const struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save();
    bool valid = member && member->live && member->shell && member->session &&
        !member->session->closing && member->session->root == member;
    cpu_interrupt_restore(saved); return valid;
}
bool console_owns_input(const struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save(); bool valid = owns(member);
    cpu_interrupt_restore(saved); return valid;
}
bool console_grant_command(struct console_member *shell, struct console_member *child)
{
    uint32_t saved = cpu_interrupt_save();
    /* Cancellation can arrive after publication but before command marking.
       Grant the metadata to that not-yet-started child; owns() still denies
       its effective authority while cancellation is pending. */
    bool valid = console_is_shell(shell) && active && child && child->live && child->session == active &&
        !active->closing && active->owner == child && child->session == shell->session &&
        child->restore == shell && !child->command;
    if (valid) child->command = true;
    cpu_interrupt_restore(saved); return valid;
}
struct console_member *console_command_shell(const struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save();
    struct console_member *shell = owns(member) && member->command ? member->session->root : 0;
    if (!console_is_shell(shell) || member->restore != shell) shell = 0;
    cpu_interrupt_restore(saved); return shell;
}
bool console_same_session(const struct console_member *first, const struct console_member *second)
{
    uint32_t saved = cpu_interrupt_save();
    bool same = first && second && first->session && first->session == second->session;
    cpu_interrupt_restore(saved); return same;
}
bool console_is_root(const struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save();
    bool root = member && member->session && member->session->root == member;
    cpu_interrupt_restore(saved); return root;
}
uint32_t console_cancellation_target(void)
{
    uint32_t saved = cpu_interrupt_save();
    struct console_member *member = active && !active->closing ? active->owner : 0;
    uint32_t id = member && member->live && !member->shell ? member->id : 0;
    cpu_interrupt_restore(saved); return id;
}
void console_cancel_member(struct console_member *member)
{
    if (!member) return;
    uint32_t saved = cpu_interrupt_save(); member->cancelled = true;
    cpu_interrupt_restore(saved);
}
bool console_member_exit(struct console_member *member)
{
    uint32_t saved = cpu_interrupt_save();
    bool discard = false;
    if (!member || !member->live) { cpu_interrupt_restore(saved); return false; }
    struct console_session *session = member->session;
    member->live = false; member->command = false;
    if (session && session->root == member) {
        discard = member->shell;
        session->closing = true;
        for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) if (session->members[i]) {
            release_return(session->members[i]); session->members[i]->command = false;
        }
        owner(session, 0);
        --member->references; session->root = 0;
        if (active == session) { active = 0; --session->references; }
    } else if (session && !session->closing) {
        /* Remove references to the exiting process without discovering a
           previous console owner through mutable parent/child relationships. */
        for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) {
            struct console_member *other = session->members[i];
            if (other && other->restore == member) {
                release_return(other); other->restore = member->restore;
                if (other->restore) ++other->restore->references;
            }
        }
        if (session->owner && (session->owner == member || session->owner->cancelled))
            owner(session, previous(session->owner));
    }
    release_return(member);
    cpu_interrupt_restore(saved); return discard;
}
bool console_detach(struct console_member *member)
{
    if (!member) return false;
    uint32_t saved = cpu_interrupt_save();
    if (member->live || member->references || member->restore) { cpu_interrupt_restore(saved); return false; }
    struct console_session *session = member->session;
    if (session) {
        unsigned i = 0;
        while (i < RUM_PROCESS_LIMIT && session->members[i] != member) ++i;
        if (i == RUM_PROCESS_LIMIT) { cpu_interrupt_restore(saved); return false; }
        session->members[i] = 0;
        if (!--session->references) *session = (struct console_session){0};
    }
    *member = (struct console_member){0};
    cpu_interrupt_restore(saved); return true;
}
bool console_request_exit(struct console_member *command, int32_t status)
{
    uint32_t saved = cpu_interrupt_save();
    bool valid = console_command_shell(command) != 0;
    if (valid) { command->session->exit_requested = true; command->session->exit_status = status; }
    cpu_interrupt_restore(saved); return valid;
}
bool console_exit_requested(const struct console_member *shell, int32_t *status)
{
    if (!status) return false;
    uint32_t saved = cpu_interrupt_save();
    bool requested = console_is_shell(shell) && shell->session->exit_requested;
    if (requested) *status = shell->session->exit_status;
    cpu_interrupt_restore(saved); return requested;
}
struct console_statistics console_stats(void)
{
    uint32_t saved = cpu_interrupt_save();
    struct console_statistics result = {0};
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) if (sessions[i].references) {
        ++result.sessions; result.closing += sessions[i].closing;
        result.session_references += sessions[i].references;
        for (unsigned j = 0; j < RUM_PROCESS_LIMIT; ++j) if (sessions[i].members[j]) {
            ++result.members; result.process_references += sessions[i].members[j]->references;
        }
    }
    if (active) {
        if (active->owner) result.owner = active->owner->id;
        if (active->root && active->root->shell) result.shell = active->root->id;
    }
    cpu_interrupt_restore(saved); return result;
}
