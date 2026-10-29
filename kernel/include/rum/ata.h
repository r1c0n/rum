#ifndef RUM_ATA_H
#define RUM_ATA_H

#include <rum/block.h>

/* One polling-only secondary-channel master (0x170 / 0x376), 512-byte
   logical sectors and LBA28. Initialize once during boot before publishing. */
struct block_result ata_initialize(void);
struct block_device *ata_device(void);

#endif
