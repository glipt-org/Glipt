#include "net.h"
#include "../object.h"
#include "../permission.h"
#include "../table.h"

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

// ---- URL Parsing ----

static bool parseUrl(const char* url, char* host, int hostLen,
                     char* port, int portLen, char* path, int pathLen,
                     bool* https) {
    *https = false;
    const char* p = url;

    if (strncmp(p, "https://", 8) == 0) {
        *https = true;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else {
        return false;
    }

    // Extract host
    const char* hostStart = p;
    while (*p && *p != '/' && *p != ':') p++;

    int hLen = (int)(p - hostStart);
    if (hLen >= hostLen) return false;
    memcpy(host, hostStart, hLen);
    host[hLen] = '\0';

    // Extract port
    if (*p == ':') {
        p++;
        const char* portStart = p;
        while (*p && *p != '/') p++;
        int pLen = (int)(p - portStart);
        if (pLen >= portLen) return false;
        memcpy(port, portStart, pLen);
        port[pLen] = '\0';
    } else {
        strncpy(port, *https ? "443" : "80", portLen - 1);
        port[portLen - 1] = '\0';
    }

    // Extract path
    if (*p == '/') {
        strncpy(path, p, pathLen - 1);
        path[pathLen - 1] = '\0';
    } else {
        strncpy(path, "/", pathLen - 1);
        path[pathLen - 1] = '\0';
    }

    return true;
}

// ---- Simple HTTP Client (plain HTTP only, no TLS) ----

static Value doHttpRequest(VM* vm, const char* method, const char* url,
                           const char* body, int bodyLen) {
    char host[256], port[16], path[2048];
    bool https;

    if (!parseUrl(url, host, 256, port, 16, path, 2048, &https)) {
        vmRaiseError(vm, "Invalid URL", "net");
        return NIL_VAL;
    }

    if (https) {
        vmRaiseError(vm, "HTTPS not supported (use HTTP or install libcurl)", "net");
        return NIL_VAL;
    }

    // Permission check
    if (!hasPermission(&vm->permissions, PERM_NET, host)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Permission denied: net \"%s\"", host);
        vmRaiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "DNS resolution failed: %s", host);
        vmRaiseError(vm, msg, "net");
        return NIL_VAL;
    }

    // Connect
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        vmRaiseError(vm, "Could not create socket", "net");
        return NIL_VAL;
    }

    // Set timeout (10 seconds)
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(sock);
        char msg[256];
        snprintf(msg, sizeof(msg), "Connection failed: %s:%s", host, port);
        vmRaiseError(vm, msg, "net");
        return NIL_VAL;
    }
    freeaddrinfo(res);

    // Build HTTP request
    char request[4096];
    int reqLen;
    if (body && bodyLen > 0) {
        reqLen = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Length: %d\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, host, bodyLen);
    } else {
        reqLen = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, host);
    }

    // Send request
    if (send(sock, request, reqLen, 0) < 0) {
        close(sock);
        vmRaiseError(vm, "Failed to send request", "net");
        return NIL_VAL;
    }

    // Send body if present
    if (body && bodyLen > 0) {
        if (send(sock, body, bodyLen, 0) < 0) {
            close(sock);
            vmRaiseError(vm, "Failed to send request body", "net");
            return NIL_VAL;
        }
    }

    // Read response
    char* response = NULL;
    int responseLen = 0;
    int responseCap = 0;
    char buf[4096];
    ssize_t n;

    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        if (responseLen + (int)n >= responseCap) {
            responseCap = (responseCap == 0) ? 8192 : responseCap * 2;
            response = realloc(response, responseCap);
        }
        memcpy(response + responseLen, buf, n);
        responseLen += (int)n;
    }
    close(sock);

    if (!response) {
        vmRaiseError(vm, "Empty response", "net");
        return NIL_VAL;
    }
    response[responseLen] = '\0';

    // Parse HTTP response: status code and body
    int status = 0;
    char* bodyStart = NULL;

    if (strncmp(response, "HTTP/", 5) == 0) {
        // Parse status
        const char* statusStr = strchr(response, ' ');
        if (statusStr) status = atoi(statusStr + 1);

        // Find body (after \r\n\r\n)
        bodyStart = strstr(response, "\r\n\r\n");
        if (bodyStart) bodyStart += 4;
    }

    // Build result map
    ObjMap* result = newMap(vm);
    vmPush(vm, OBJ_VAL(result));

    tableSet(&result->table,
        copyString(vm, "status", 6), NUMBER_VAL(status));

    if (bodyStart) {
        int bLen = responseLen - (int)(bodyStart - response);
        tableSet(&result->table,
            copyString(vm, "body", 4),
            OBJ_VAL(copyString(vm, bodyStart, bLen)));
    } else {
        tableSet(&result->table,
            copyString(vm, "body", 4),
            OBJ_VAL(copyString(vm, response, responseLen)));
    }

    free(response);
    vmPop(vm);
    return OBJ_VAL(result);
}

// ---- HTTP Methods ----

static Value netGetNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;
    return doHttpRequest(vm, "GET", AS_CSTRING(args[0]), NULL, 0);
}

static Value netPostNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
    ObjString* body = AS_STRING(args[1]);
    return doHttpRequest(vm, "POST", AS_CSTRING(args[0]), body->chars, body->length);
}

static Value netPutNative(VM* vm, int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
    ObjString* body = AS_STRING(args[1]);
    return doHttpRequest(vm, "PUT", AS_CSTRING(args[0]), body->chars, body->length);
}

static Value netDeleteNative(VM* vm, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return NIL_VAL;
    return doHttpRequest(vm, "DELETE", AS_CSTRING(args[0]), NULL, 0);
}

// ---- DNS ----

static Value netResolveNative(VM* vm, int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* hostname = AS_CSTRING(args[0]);

    if (!hasPermission(&vm->permissions, PERM_NET, hostname)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Permission denied: net \"%s\"", hostname);
        vmRaiseError(vm, msg, "permission");
        return NIL_VAL;
    }

    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        vmRaiseError(vm, "DNS resolution failed", "net");
        return NIL_VAL;
    }

    ObjList* list = newList(vm);
    vmPush(vm, OBJ_VAL(list));

    for (p = res; p != NULL; p = p->ai_next) {
        char ip[INET6_ADDRSTRLEN];
        if (p->ai_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)p->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        } else {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)p->ai_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
        }
        listAppend(vm, list, OBJ_VAL(copyString(vm, ip, (int)strlen(ip))));
    }

    freeaddrinfo(res);
    vmPop(vm);
    return OBJ_VAL(list);
}

// ---- Module Registration ----

void registerNetModule(VM* vm) {
    ObjMap* net = newMap(vm);
    vmPush(vm, OBJ_VAL(net));

    defineModuleNative(vm, net, "get", netGetNative, -1);
    defineModuleNative(vm, net, "post", netPostNative, -1);
    defineModuleNative(vm, net, "put", netPutNative, -1);
    defineModuleNative(vm, net, "delete", netDeleteNative, -1);
    defineModuleNative(vm, net, "resolve", netResolveNative, 1);

    ObjString* name = copyString(vm, "net", 3);
    tableSet(&vm->globals, name, OBJ_VAL(net));
    vmPop(vm);
}
