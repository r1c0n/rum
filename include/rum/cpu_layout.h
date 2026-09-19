#ifndef RUM_CPU_LAYOUT_H
#define RUM_CPU_LAYOUT_H

/* Also included by preprocessed assembly. Keep selector values unsuffixed. */
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR   0x1B
#define USER_DATA_SELECTOR   0x23
#define TSS_SELECTOR         0x28
#define GDT_ENTRY_COUNT      6

#endif
