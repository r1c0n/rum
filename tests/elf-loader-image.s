.section .rodata, "a"
.balign 4
.global elf_loader_asset_start, elf_loader_asset_end
elf_loader_asset_start:
    .incbin "build/user/ramfs/hello.elf"
elf_loader_asset_end:
.section .note.GNU-stack, "", @progbits
