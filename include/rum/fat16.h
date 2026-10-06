#ifndef RUM_FAT16_H
#define RUM_FAT16_H
#include <rum/fs.h>

#define FAT16_SECTOR_SIZE 512u
#define FAT16_FAT_SECTOR_LIMIT 256u
#define FAT16_CLUSTER_SECTOR_LIMIT 64u
#define FAT16_DIRECTORY_ENTRY_LIMIT 65536u

struct fat16_information {
    uint32_t sectors, clusters, cluster_bytes;
    uint32_t fat_start, fat_sectors, root_start, root_entries, data_start;
    bool mounted;
};
/* One unpartitioned volume, mounted read-only at /disk. Boot/foreground only.
   Never formats or writes the device. Failed mounting releases its allocations.
   Keep the device alive and its contents unchanged until successfully unmounted. */
enum fs_error fat16_mount(struct block_device *device);
enum fs_error fat16_unmount(void);
struct fat16_information fat16_info(void);
#endif
