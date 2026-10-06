#ifndef RUM_FAT16_INTERNAL_H
#define RUM_FAT16_INTERNAL_H
#include <rum/fat16.h>

#define ROOT_ID UINT64_C(1)
#define ENTRY_BYTES 32u
#define ENTRIES_PER_SECTOR (FAT16_SECTOR_SIZE / ENTRY_BYTES)
#define FIRST_RESERVED 0xfff0u
#define FIRST_END 0xfff8u
#define MAX_CLUSTERS 65524u
struct pin { uint64_t id; uint32_t count; };
struct description { struct fs_node node; uint32_t first, length; };
struct fat16_volume {
    struct block_device *device;
    struct fat16_information info;
    unsigned char *fat;
    unsigned char sector[FAT16_SECTOR_SIZE];
    unsigned char visited[(MAX_CLUSTERS + 2u + 7u) / 8u];
    struct pin pins[FS_OBJECT_LIMIT];
    struct fs_io_result last_io;
    uint32_t cached_sector;
    bool ready, busy, cache_valid;
};
extern struct fat16_volume fat16_volume;

static inline uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static inline uint32_t u32(const unsigned char *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
static inline bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
enum fs_error fat16_read_sectors(uint32_t, uint32_t, void *);
enum fs_error fat16_sector(uint32_t);
bool fat16_valid_cluster(uint32_t);
uint16_t fat16_next_cluster(uint32_t);
enum fs_error fat16_chain(uint32_t, uint32_t *);
bool fat16_slot_offset(uint64_t, uint64_t *);
enum fs_error fat16_decode(const unsigned char *, uint64_t, struct description *, char *);
enum fs_error fat16_describe(uint64_t, struct description *);
uint32_t fat16_cluster_at(const struct description *, uint32_t);
uint32_t fat16_cluster_sector(uint32_t);
uint64_t fat16_directory_slots(const struct description *);
enum fs_error fat16_directory_slot(const struct description *, uint64_t, uint64_t *);
enum fs_error fat16_directory_entry(const struct description *, uint64_t, struct description *, char *);
#endif
