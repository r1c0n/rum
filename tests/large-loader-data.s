.section .rodata.large, "a"
.global large_rodata
large_rodata:
.set large_index, 0
.rept 131079
    .byte (large_index * 17 + 3) & 255
    .set large_index, large_index + 1
.endr
.section .note.GNU-stack, "", @progbits
