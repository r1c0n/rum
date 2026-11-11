#ifndef RUM_FS_POLICY_H
#define RUM_FS_POLICY_H
#include <rum/fs.h>
#define FS_SYSTEM_OBJECT_LIMIT 512u
/* Mount-local backend identities, including the reserved RUM root itself.
   Invalid/incomplete indexes fail closed for untrusted mutations. */
struct fs_system_index { uint64_t ids[FS_SYSTEM_OBJECT_LIMIT]; unsigned count; bool valid; };
bool fs_set_authority_policy(bool (*system_write)(void), bool (*device_control)(void));
bool fs_system_write_allowed(void);
bool fs_device_control_allowed(void);
bool fs_system_identity(const struct fs_system_index *, uint64_t);
enum fs_error fs_system_index_build(struct fs_system_index *, const struct fs_backend *);
#endif
