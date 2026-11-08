#include "files.h"
static int identity(const struct rum_file_information *a, const struct rum_file_information *b)
{
    return a->volume == b->volume && a->generation == b->generation &&
           a->object_lo == b->object_lo && a->object_hi == b->object_hi;
}
rum_result_t file_copy(const char *source, const char *destination, int replace, struct file_copy_status *status)
{
    *status = (struct file_copy_status){0};
    rum_result_t input = rum_open(source, RUM_OPEN_READ, 0);
    if (input < 0) return input;
    struct rum_file_information before, target, after;
    rum_result_t output = -1, error = rum_fstat((rum_handle_t)input, &before);
    if (!error && before.kind != RUM_ENTRY_FILE) error = -RUM_EISDIR;
    if (!error && before.size_hi) error = -RUM_E2BIG;
    if (!error) {
        error = rum_stat(destination, &target);
        if (!error) {
            if (identity(&before, &target)) { status->same_file = 1; error = -RUM_EINVAL; }
            else if (target.kind != RUM_ENTRY_FILE) error = -RUM_EISDIR;
            else if (!replace) error = -RUM_EEXIST;
        } else if (error == -RUM_ENOENT) error = 0;
    }
    if (!error) {
        output = rum_open(destination, RUM_OPEN_WRITE, RUM_OPEN_CREATE |
            (replace ? RUM_OPEN_TRUNCATE : RUM_OPEN_EXCLUSIVE));
        if (output < 0) error = output;
        else status->output_opened = 1;
    }
    unsigned char buffer[512];
    uint32_t remaining = error ? 0 : before.size_lo;
    while (!error && remaining) {
        uint32_t amount = remaining < sizeof buffer ? remaining : sizeof buffer;
        rum_result_t count = rum_read((rum_handle_t)input, buffer, amount);
        if (count <= 0 || (uint32_t)count > amount) { error = count < 0 ? count : -RUM_EIO; break; }
        uint32_t offset = 0;
        while (offset < (uint32_t)count) {
            rum_result_t done = rum_write((rum_handle_t)output, buffer + offset, (uint32_t)count - offset);
            if (done <= 0 || (uint32_t)done > (uint32_t)count - offset) { error = done < 0 ? done : -RUM_EIO; break; }
            offset += (uint32_t)done;
        }
        remaining -= (uint32_t)count;
    }
    if (!error) {
        error = rum_fstat((rum_handle_t)input, &after);
        if (!error && (!identity(&before, &after) || before.size_lo != after.size_lo || before.size_hi != after.size_hi))
            error = -RUM_EIO;
    }
    if (!error) error = rum_flush((rum_handle_t)output);
    if (output >= 0) { rum_result_t closed = rum_close((rum_handle_t)output); if (!error) error = closed; }
    rum_result_t closed = rum_close((rum_handle_t)input);
    return error ? error : closed;
}
void file_copy_error(const char *command, rum_result_t error, const struct file_copy_status *status)
{
    if (status->same_file) { tool_print(command); tool_print(": source and destination are the same file.\n"); }
    else tool_error(command, error);
    if (status->output_opened) { tool_print(command); tool_print(": destination may contain an incomplete copy.\n"); }
}
