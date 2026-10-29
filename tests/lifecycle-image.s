.section .rodata, "a"
.balign 4
.global lifecycle_asset_start, lifecycle_asset_end
lifecycle_asset_start:
    .incbin "build/tests/user/lifecycle.elf"
lifecycle_asset_end:
.section .note.GNU-stack, "", @progbits
