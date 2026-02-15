#include "permission.h"

void initPermissions(PermissionSet* set) {
    set->permissions = NULL;
    set->count = 0;
    set->capacity = 0;
    set->allowAll = false;
}

void freePermissions(PermissionSet* set) {
    for (int i = 0; i < set->count; i++) {
        free(set->permissions[i].target);
    }
    free(set->permissions);
    initPermissions(set);
}

void addPermission(PermissionSet* set, PermissionType type, const char* target, int length) {
    if (set->count >= set->capacity) {
        set->capacity = set->capacity < 8 ? 8 : set->capacity * 2;
        set->permissions = (Permission*)realloc(set->permissions,
            sizeof(Permission) * set->capacity);
    }

    set->permissions[set->count].type = type;
    set->permissions[set->count].target = (char*)malloc(length + 1);
    memcpy(set->permissions[set->count].target, target, length);
    set->permissions[set->count].target[length] = '\0';
    set->count++;
}

// Simple glob matching with * wildcards
static bool globMatch(const char* pattern, const char* text) {
    while (*pattern && *text) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true;
            while (*text) {
                if (globMatch(pattern, text)) return true;
                text++;
            }
            return false;
        }
        if (*pattern != *text) return false;
        pattern++;
        text++;
    }

    while (*pattern == '*') pattern++;
    return *pattern == '\0' && *text == '\0';
}

bool hasPermission(PermissionSet* set, PermissionType type, const char* target) {
    if (set->allowAll) return true;

    for (int i = 0; i < set->count; i++) {
        if (set->permissions[i].type == type) {
            if (globMatch(set->permissions[i].target, target)) {
                return true;
            }
        }
    }
    return false;
}

const char* permissionTypeName(PermissionType type) {
    switch (type) {
        case PERM_EXEC:  return "exec";
        case PERM_NET:   return "net";
        case PERM_READ:  return "read";
        case PERM_WRITE: return "write";
        case PERM_ENV:   return "env";
    }
    return "unknown";
}
