.section .rodata, "a"
.balign 4
.global large_loader_asset_start, large_loader_asset_end
large_loader_asset_start:
    .incbin "build/tests/user/large-loader.elf"
large_loader_asset_end:
.section .note.GNU-stack, "", @progbits
