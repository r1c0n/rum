.section .rodata, "a"
.balign 4
.global vm_regions_asset_start, vm_regions_asset_end
vm_regions_asset_start:
    .incbin "build/tests/user/vm-regions.elf"
vm_regions_asset_end:
.section .note.GNU-stack, "", @progbits
