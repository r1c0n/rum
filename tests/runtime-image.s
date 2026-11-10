.section .rodata, "a"
.balign 4
.global runtime_asset_start, runtime_asset_end
runtime_asset_start:
    .incbin "build/tests/user/runtime.elf"
runtime_asset_end:
.section .note.GNU-stack, "", @progbits
