.section .rodata
.global integration_asset_start, integration_asset_end
integration_asset_start:
.incbin "build/tests/user/integration.elf"
integration_asset_end:
.section .note.GNU-stack,"",@progbits
