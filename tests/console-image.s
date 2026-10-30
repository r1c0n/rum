.section .rodata
.global console_asset_start, console_asset_end
console_asset_start:
.incbin "build/tests/user/console.elf"
console_asset_end:
.section .note.GNU-stack,"",@progbits
