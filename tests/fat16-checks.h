#ifndef RUM_TEST_FAT16_CHECKS_H
#define RUM_TEST_FAT16_CHECKS_H
#include <rum/fat16.h>
#define FAT16_TEST_BYTES 10037u
#define FAT16_TEST_LISTING_CAPACITY 8192u
extern unsigned char fat16_test_data[FAT16_TEST_BYTES];
extern char fat16_test_listing[FAT16_TEST_LISTING_CAPACITY];
extern uint32_t fat16_test_listing_length;
void fat16_check(bool condition, const char *name);
void fat16_report(const char *phase, enum fs_error error);
void fat16_checks(struct block_device *device, const char *phase, const char *path);
#endif
