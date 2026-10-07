.section .rodata
.balign 4
.global fs_hello_start, fs_hello_end
fs_hello_start:
.incbin "build/user/system/hello.elf"
fs_hello_end:
.balign 4
.global fs_fault_start, fs_fault_end
fs_fault_start:
.incbin "build/user/system/fault.elf"
fs_fault_end:
.balign 4
.global fs_spin_start, fs_spin_end
fs_spin_start:
.incbin "build/user/system/spin.elf"
fs_spin_end:
.section .note.GNU-stack,"",@progbits
