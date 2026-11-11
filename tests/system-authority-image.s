.section .rodata, "a"
.balign 4
.global authority_asset_start, authority_asset_end
authority_asset_start:
    .incbin "build/tests/user/system-authority.elf"
authority_asset_end:
.section .note.GNU-stack, "", @progbits
