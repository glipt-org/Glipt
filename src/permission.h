#ifndef glipt_permission_h
#define glipt_permission_h

#include "common.h"

typedef enum {
    PERM_EXEC,
    PERM_NET,
    PERM_READ,
    PERM_WRITE,
    PERM_ENV,
} PermissionType;

typedef struct {
    PermissionType type;
    char* target;       // target pattern (supports * wildcards)
} Permission;

typedef struct {
    Permission* permissions;
    int count;
    int capacity;
    bool allowAll;      // --allow-all flag
} PermissionSet;

void initPermissions(PermissionSet* set);
void freePermissions(PermissionSet* set);
void addPermission(PermissionSet* set, PermissionType type, const char* target, int length);
bool hasPermission(PermissionSet* set, PermissionType type, const char* target);
const char* permissionTypeName(PermissionType type);

#endif
