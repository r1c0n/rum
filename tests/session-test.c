#include <assert.h>
#include <stdio.h>
#include <rum/session.h>
#include <rum/process_limits.h>

static void empty(void)
{
    struct console_statistics s = console_stats();
    assert(!s.sessions && !s.closing && !s.members && !s.session_references &&
           !s.process_references && !s.owner && !s.shell);
}
static void nested(void)
{
    struct console_member root = {0}, background = {0}, child = {0}, nested = {0};
    assert(console_attach(&root, 1, NULL, true) && root.references == 2 && console_cancellation_target() == 1);
    assert(console_mark_shell(&root) && !console_mark_shell(&root) && !console_cancellation_target());
    assert(!console_attach(&root, 1, NULL, true));
    assert(console_attach(&background, 2, &root, false) && !console_owns_input(&background) &&
           !console_can_launch(&background, true) && !console_grant_command(&root, &background));
    assert(console_attach(&child, 3, &root, true) && console_owns_input(&child) && !console_owns_input(&root));
    assert(console_grant_command(&root, &child) && console_command_shell(&child) == &root);
    assert(console_attach(&nested, 4, &child, true) && console_cancellation_target() == 4 &&
           !console_command_shell(&child) && !console_command_shell(&nested));
    assert(!console_grant_command(&child, &nested) && !console_request_exit(&nested, 75));
    assert(!console_detach(&root) && !console_detach(&nested));
    console_cancel_member(&nested);
    assert(!console_owns_input(&nested) && console_cancellation_target() == 4);
    /* An unrelated exit must not transfer input away from an owner that is
       still completing cancellation in its own kernel continuation. */
    struct console_member unrelated = {0};
    assert(console_attach(&unrelated, 6, &background, false));
    assert(!console_member_exit(&unrelated) && console_detach(&unrelated) && console_cancellation_target() == 4);
    assert(!console_member_exit(&nested) && !nested.references && console_owns_input(&child) &&
           console_cancellation_target() == 3 && console_detach(&nested));
    assert(console_command_shell(&child) == &root && console_request_exit(&child, 75));
    int32_t status = 0;
    assert(console_exit_requested(&root, &status) && status == 75);
    assert(!console_member_exit(&child) && !child.references && console_owns_input(&root) && console_detach(&child));
    assert(console_member_exit(&root) && !root.references && !console_cancellation_target());
    struct console_member copy = root;
    assert(!console_detach(&copy) && console_detach(&root));
    struct console_statistics s = console_stats();
    assert(s.sessions == 1 && s.closing == 1 && s.members == 1 && s.session_references == 1 && !s.process_references);
    struct console_member successor = {0};
    assert(console_attach(&successor, 5, NULL, true) && console_mark_shell(&successor) &&
           successor.session != background.session && !console_same_session(&successor, &background));
    assert(!console_can_launch(&background, false) && !console_owns_input(&background) &&
           !console_grant_command(&successor, &background));
    assert(!console_member_exit(&background) && console_detach(&background) && console_owns_input(&successor));
    assert(console_member_exit(&successor) && console_detach(&successor));
    empty();
}
static void exiting_parent(void)
{
    struct console_member root = {0}, parent = {0}, child = {0};
    assert(console_attach(&root, 10, NULL, true) && console_mark_shell(&root) &&
           console_attach(&parent, 11, &root, true) && console_attach(&child, 12, &parent, true));
    console_cancel_member(&parent); console_cancel_member(&child);
    assert(!console_member_exit(&parent) && !parent.references && console_detach(&parent) &&
           child.restore == &root && console_owns_input(&root));
    assert(!console_member_exit(&child) && console_detach(&child) && root.references == 2);
    assert(console_member_exit(&root) && console_detach(&root)); empty();
}
static void cancelled_publication(void)
{
    struct console_member root = {0}, child = {0};
    assert(console_attach(&root, 20, NULL, true) && console_mark_shell(&root) && console_attach(&child, 21, &root, true));
    console_cancel_member(&child);
    assert(console_grant_command(&root, &child) && !console_command_shell(&child));
    assert(!console_member_exit(&child) && console_detach(&child) && console_owns_input(&root));
    assert(console_member_exit(&root) && console_detach(&root)); empty();
}
static void bounds(void)
{
    struct console_member roots[RUM_PROCESS_LIMIT] = {0}, extra = {0};
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) {
        assert(console_attach(&roots[i], 100 + i, NULL, true));
        assert(!console_member_exit(&roots[i])); /* Temporary kernel-launched root. */
    }
    assert(!console_can_launch(NULL, true) && !console_attach(&extra, 999, NULL, true) && !extra.live);
    for (unsigned i = 0; i < RUM_PROCESS_LIMIT; ++i) assert(console_detach(&roots[i]));
    empty();
    assert(console_attach(&roots[0], 1000, NULL, true));
    for (unsigned i = 1; i < RUM_PROCESS_LIMIT; ++i) assert(console_attach(&roots[i], 1000 + i, &roots[0], false));
    assert(!console_can_launch(&roots[0], false) && !console_attach(&extra, 9999, &roots[0], false) && !extra.live);
    assert(!console_member_exit(&roots[0]) && console_detach(&roots[0]));
    for (unsigned i = 1; i < RUM_PROCESS_LIMIT; ++i) assert(!console_member_exit(&roots[i]) && console_detach(&roots[i]));
    empty();
    assert(console_attach(&extra, 2000, NULL, false) && !console_can_launch(&extra, true) && !console_owns_input(&extra));
    assert(!console_member_exit(&extra) && console_detach(&extra)); empty();
}
static void pipeline(void)
{
    struct console_member shell = {0}, a = {0}, b = {0}, bg = {0}, late = {0};
    assert(console_attach(&shell, 3000, NULL, true) && console_mark_shell(&shell));
    assert(console_attach(&bg, 3001, &shell, false));
    assert(!console_pipeline_begin(&bg) && !console_pipeline_join(&shell, &bg));
    assert(console_pipeline_begin(&shell) && !console_pipeline_begin(&shell));
    assert(!console_owns_input(&shell) && !console_can_launch(&shell, true));
    assert(console_attach(&a, 3002, &shell, false) && console_pipeline_join(&shell, &a));
    assert(console_attach(&b, 3003, &shell, false) && console_pipeline_join(&shell, &b));
    assert(console_owns_input(&a) && !console_owns_input(&b) && !console_owns_input(&bg));
    assert(!console_pipeline_end(&shell) && !console_grant_command(&shell, &a));
    assert(console_pipeline_cancel() && console_pipeline_cancelled(&shell));
    assert(console_pipeline_member(&a) && console_pipeline_member(&b) && !console_pipeline_member(&bg));
    console_cancel_member(&a); console_cancel_member(&b);
    assert(!console_member_exit(&a) && console_detach(&a) && !console_owns_input(&b));
    assert(console_attach(&late, 3004, &shell, false) && console_pipeline_join(&shell, &late));
    assert(console_pipeline_cancelled(&shell) && !console_owns_input(&late));
    assert(!console_member_exit(&late) && console_detach(&late));
    assert(!console_member_exit(&b) && console_detach(&b));
    assert(console_pipeline_end(&shell) && console_owns_input(&shell) && !console_pipeline_cancel());
    assert(console_pipeline_begin(&shell) && !console_pipeline_cancelled(&shell));
    assert(console_attach(&a, 3005, &shell, false) && console_pipeline_join(&shell, &a));
    assert(console_member_exit(&shell) && !a.references && !console_owns_input(&a));
    assert(console_detach(&shell));
    assert(!console_member_exit(&a) && console_detach(&a));
    assert(!console_member_exit(&bg) && console_detach(&bg)); empty();
}
static void job_focus(void)
{
    struct console_member shell = {0}, first = {0}, second = {0}, command = {0};
    assert(console_attach(&shell, 4000, NULL, true) && console_mark_shell(&shell));
    assert(console_attach(&first, 4001, &shell, false) && console_attach(&second, 4002, &shell, false));
    first.job_id = second.job_id = 42;
    assert(!console_job_focus(&first, 42, &first) && !console_job_target());
    assert(console_job_focus(&shell, 42, NULL) && console_job_input(&shell, 42, &first));
    assert(console_job_target() == 42 && console_owns_input(&first) && !console_owns_input(&shell));
    assert(!console_owns_input(&second) && !console_pipeline_begin(&shell));
    assert(!console_member_exit(&first) && console_detach(&first) && !console_owns_input(&second));
    assert(console_job_target() == 42); console_job_end(&second, 42);
    assert(console_owns_input(&shell) && !console_job_target());
    assert(console_attach(&command, 4003, &shell, true) && console_grant_command(&shell, &command));
    assert(console_job_focus(&command, 42, &second) && console_owns_input(&second) && !console_owns_input(&command));
    assert(console_command_shell(&command) == &shell);
    console_job_end(&command, 42);
    assert(console_owns_input(&command) && !console_member_exit(&command) && console_detach(&command));
    assert(console_job_focus(&shell, 42, &second));
    assert(console_member_exit(&shell) && !second.references && !console_job_target() && console_detach(&shell));
    assert(!console_member_exit(&second) && console_detach(&second)); empty();
}
int main(void)
{
    empty();
    for (unsigned i = 0; i < 64; ++i) { nested(); exiting_parent(); cancelled_publication(); }
    bounds(); pipeline(); job_focus();
    puts("PASS: console session references, nested ownership, cancellation, closing sessions and bounded reuse");
    return 0;
}
