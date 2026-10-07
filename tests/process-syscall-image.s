.section .rodata, "a"
.balign 4
.global process_syscall_asset_start, process_syscall_asset_end
process_syscall_asset_start:
    .incbin "build/tests/user/process-syscall.elf"
process_syscall_asset_end:
.section .note.GNU-stack, "", @progbits
