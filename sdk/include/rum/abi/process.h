#ifndef RUM_ABI_PROCESS_H
#define RUM_ABI_PROCESS_H

#include <rum/abi/types.h>
#include <rum/abi/filesystem.h>

#define RUM_ABI_ARGUMENT_LIMIT 32 /* Includes argv[0]. */
#define RUM_ABI_ARGUMENT_BYTES 4096 /* Includes every string's NUL. */
#define RUM_PROCESS_ABI_VERSION 1
#define RUM_COMMAND_ABI_VERSION 2
#define RUM_COMMAND_ARGV_VERSION 3 /* Same packet, text/reserved both zero. */
#define RUM_LIFECYCLE_ABI_VERSION 1
#define RUM_SPAWN_MAPPED_VERSION 1
#define RUM_SPAWN_MAPPING_LIMIT RUM_ABI_HANDLE_LIMIT
#define RUM_SPAWN_NO_DEFAULT_STREAMS 1u
#define RUM_SPAWN_PIPELINE 2u /* Shell only, inside a foreground pipeline scope. */
#define RUM_SPAWN_JOB 4u /* Shell only, joins its currently building session job. */
#define RUM_PROCESS_LIST_VERSION 1
#define RUM_PROCESS_LIST_LIMIT 16
#define RUM_PROCESS_NAME_CAPACITY 64
#define RUM_PROCESS_RUNNABLE 1
#define RUM_PROCESS_RUNNING 2
#define RUM_PROCESS_WAITING 3
#define RUM_PROCESS_ENDED 4
#define RUM_PROCESS_CANCELLING 1
#define RUM_WAIT_ANY 0 /* Any direct child, never another process's child. */
#define RUM_WAIT_NOHANG 1
#define RUM_TERMINATE_SESSION 1 /* Delegated foreground shell-command authority. */
#define RUM_COMMAND_TEXT_CAPACITY 256
#define RUM_RUN_COMMAND 1
#define RUM_SESSION_CHDIR 1
#define RUM_SESSION_EXIT 2
#define RUM_SESSION_RECOVERY 3
#define RUM_SESSION_PIPELINE_BEGIN 4 /* Initial shell only; no path. */
#define RUM_SESSION_PIPELINE_END 5 /* All scoped children must have exited. */
#define RUM_PROCESS_EXITED 1
#define RUM_PROCESS_FAULTED 2
#define RUM_PROCESS_CANCELLED 3
#define RUM_SHELL_RECOVERY_STATUS 75 /* Initial shell's explicit recovery request. */

#ifndef __ASSEMBLER__
#include <stddef.h>

/* A snapshot of user processes only. Parent PID zero denotes kernel adoption.
   Names are sanitized executable basenames, never addresses or full paths. */
struct rum_process_entry {
    rum_pid_t process_id, parent_id;
    uint32_t state, flags;
    char name[RUM_PROCESS_NAME_CAPACITY];
};
/* capacity 0..LIMIT; count/total are output and must initially be zero.
   entries covers capacity records and must not overlap this packet. A zero
   capacity requires a zero entries address and returns only the total. */
struct rum_process_list_request {
    uint32_t version, size;
    rum_address_t entries;
    uint32_t capacity, count, total, reserved[2];
};
_Static_assert(sizeof(struct rum_process_entry) == 80 && sizeof(struct rum_process_list_request) == 32,
               "process query packet layout");

/* Launch packet: copied as bounded bytes, never followed as host/kernel
   pointers. argc is 1..32; offsets describe tightly packed strings in order,
   beginning at offset zero. string_bytes counts exactly those strings. */
struct rum_arguments {
    uint32_t argc;
    uint32_t string_bytes;
    uint32_t offsets[RUM_ABI_ARGUMENT_LIMIT];
    char strings[RUM_ABI_ARGUMENT_BYTES];
};

/* Synchronous foreground launch. All addresses are copied/validated before
   loading. flags/reserved are zero; only the calling process can wait/reap. */
struct rum_run_request {
    uint32_t version, size;
    rum_address_t path, arguments, result;
    uint32_t flags, reserved[2];
};
/* Initial interactive shell only: version 3 uses argv exclusively and text
   must be zero. Legacy version 2 retains raw text: argv has its normal layout;
   text is
   the exact unparsed argument tail, copied before launch (max 255 bytes).
   The direct child may request a session directory change or shell exit. */
struct rum_command_request {
    uint32_t version, size;
    rum_address_t path, arguments, result;
    uint32_t flags;
    rum_address_t text;
    uint32_t reserved;
};
_Static_assert(sizeof(struct rum_command_request) == sizeof(struct rum_run_request) &&
               offsetof(struct rum_command_request, text) == offsetof(struct rum_run_request, reserved),
               "command launch packet layout");
/* Initialize version, size and reserved. Other words are output only. Faults
   expose a vector, never an instruction, stack, kernel or physical address. */
struct rum_process_result {
    uint32_t version, size;
    rum_pid_t process_id;
    uint32_t termination;
    int32_t status;
    uint32_t fault_vector, reserved;
};
/* Asynchronous, nonforeground launch. flags/reserved must be zero. Returns a
   positive PID after complete publication; there is no output user pointer. */
struct rum_spawn_request {
    uint32_t version, size;
    rum_address_t path, arguments;
    uint32_t flags, reserved[3];
};
/* Only named sources are inherited. Sources may repeat; targets must be unique.
   Standard target 0 requires read access, 1/2 write access, and no directories.
   CLOSPAWN sources are forbidden. Child descriptor flags start at zero. */
struct rum_handle_mapping {
    rum_handle_t source, target;
    uint32_t reserved[2];
};
/* By default unmapped 0..2 are fresh console streams. NO_DEFAULT_STREAMS starts
   with an empty table. All other unmapped descriptors are closed. Zero count
   requires a zero mappings address. PIPELINE requires an active scope owned by
   the initial shell; its first joined child receives console input only.
   JOB joins the initial shell's building session job (foreground or background)
   and cannot be combined with PIPELINE. Other mapped launches grant no
   foreground or shell-command authority. */
struct rum_spawn_mapped_request {
    uint32_t version, size;
    rum_address_t path, arguments, mappings;
    uint32_t mapping_count, flags, reserved;
};
_Static_assert(sizeof(struct rum_handle_mapping) == 16 &&
               sizeof(struct rum_spawn_mapped_request) == 32 &&
               offsetof(struct rum_spawn_mapped_request, mapping_count) == 20, "mapped spawn layout");
/* Returns 1 with a copied result and consumes that child's exit record, or 0
   for NOHANG with live children but no completion. Errors/0 leave output intact.
   Initialize the result's existing process ABI version, size and reserved. */
struct rum_wait_request {
    uint32_t version, size;
    rum_pid_t process_id;
    rum_address_t result;
    uint32_t flags, reserved[3];
};
/* Request cancellation at a safe boundary. Zero flags permits direct children
   only. SESSION additionally requires a marked foreground shell command and
   permits another direct child of that command's shell. No arbitrary kill. */
struct rum_terminate_request {
    uint32_t version, size;
    rum_pid_t process_id;
    uint32_t flags, reserved[2];
};
_Static_assert(sizeof(struct rum_spawn_request) == 32 &&
               sizeof(struct rum_wait_request) == 32 &&
               sizeof(struct rum_terminate_request) == 24 &&
               offsetof(struct rum_wait_request, result) == 12, "lifecycle packet layout");
_Static_assert(sizeof(struct rum_run_request) == 32 && sizeof(struct rum_process_result) == 28,
               "process packet layout");

_Static_assert(offsetof(struct rum_arguments, offsets) == 8 &&
               offsetof(struct rum_arguments, strings) == 136 &&
               sizeof(struct rum_arguments) == 4232, "argument packet layout");
#endif
#endif
