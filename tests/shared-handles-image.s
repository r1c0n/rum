.section .rodata, "a"
.balign 4
.global shared_handles_asset_start, shared_handles_asset_end
shared_handles_asset_start:
    .incbin "build/tests/user/shared-handles.elf"
shared_handles_asset_end:
.section .note.GNU-stack, "", @progbits
