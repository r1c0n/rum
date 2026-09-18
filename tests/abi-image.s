.section .rodata, "a"
.balign 4
.global abi_asset_start, abi_asset_end
abi_asset_start:
#if RUM_USER_CASE == 2
    .incbin "build/user/ramfs/hello.elf"
#else
    .incbin "build/tests/user/abi-probe.elf"
#endif
abi_asset_end:
.section .note.GNU-stack, "", @progbits
