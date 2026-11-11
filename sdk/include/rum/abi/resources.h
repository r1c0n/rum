#ifndef RUM_ABI_RESOURCES_H
#define RUM_ABI_RESOURCES_H
#include <rum/abi/types.h>
#define RUM_RESOURCE_VERSION 1u
#define RUM_RESOURCE_TYPE_COUNT 8u /* Indices are RUM_OBJECT_*; zero is unused. */
#ifndef __ASSEMBLER__
/* Caller-local table snapshot. No target PID, tokens, paths, addresses, global
   reference counts or other processes' entries. Zero every field except
   version/size on input. Types count unique objects, not aliases. References
   count only this process's descriptor and typed-handle table entries. */
struct rum_resource_information {
    uint32_t version, size, flags, reserved;
    uint32_t descriptors, typed_handles, objects, table_references;
    uint32_t types[RUM_RESOURCE_TYPE_COUNT], reserved_tail[4];
};
_Static_assert(sizeof(struct rum_resource_information) == 80, "resource snapshot wire layout");
#endif
#endif
