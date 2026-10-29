.section .rodata, "a"
.balign 4
.global sleep_asset_start, sleep_asset_end
sleep_asset_start:
    .incbin "build/tests/user/sleep.elf"
sleep_asset_end:
.section .note.GNU-stack, "", @progbits
