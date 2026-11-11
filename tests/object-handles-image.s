.section .rodata, "a"
.balign 4
.global object_handles_asset_start, object_handles_asset_end
object_handles_asset_start:
    .incbin "build/tests/user/object-handles.elf"
object_handles_asset_end:
.section .note.GNU-stack, "", @progbits
