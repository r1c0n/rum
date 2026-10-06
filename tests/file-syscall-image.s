.section .rodata, "a"
.balign 4
.global file_syscall_asset_start, file_syscall_asset_end
file_syscall_asset_start:
    .incbin "build/tests/user/file-syscall.elf"
file_syscall_asset_end:
.section .note.GNU-stack, "", @progbits
