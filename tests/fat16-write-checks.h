#ifndef RUM_FAT16_WRITE_CHECKS_H
#define RUM_FAT16_WRITE_CHECKS_H
#include <rum/fat16.h>
#define FAT16_WRITE_TEST_BYTES 20000u
void fat16_write_check(bool, const char *);
void fat16_write_checks(struct block_device *);
void fat16_write_verify(struct block_device *);
enum fs_error fat16_write_operation(const char *, size_t *);
#endif
