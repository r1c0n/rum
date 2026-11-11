.section .rodata, "a"
.balign 4
.global process_objects_asset_start, process_objects_asset_end
process_objects_asset_start:
    .incbin "build/tests/user/process-objects.elf"
process_objects_asset_end:
.section .note.GNU-stack, "", @progbits
