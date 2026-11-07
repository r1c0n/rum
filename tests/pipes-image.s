.section .rodata, "a"
.balign 4
.global pipes_asset_start, pipes_asset_end
pipes_asset_start:
    .incbin "build/tests/user/pipes.elf"
pipes_asset_end:
.section .note.GNU-stack, "", @progbits
