#include "dataformat.h"
#include "object.h"
#include "table.h"
#include "memory.h"
#include "vm.h"

#include <ctype.h>

// ---- JSON Parser ----

typedef struct {
    const char* source;
    int length;
    int pos;
    VM* vm;
    bool hadError;
} JSONParser;

static void jsonError(JSONParser* p, const char* message) {
    if (!p->hadError) {
        fprintf(stderr, "JSON parse error at position %d: %s\n", p->pos, message);
        p->hadError = true;
    }
}

static char jsonPeek(JSONParser* p) {
    if (p->pos >= p->length) return '\0';
    return p->source[p->pos];
}

static void jsonSkipWhitespace(JSONParser* p) {
    while (p->pos < p->length) {
        char c = p->source[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p->pos++;
        } else {
            break;
        }
    }
}

static bool jsonMatch(JSONParser* p, char expected) {
    if (p->pos >= p->length || p->source[p->pos] != expected) return false;
    p->pos++;
    return true;
}

static Value jsonParseValue(JSONParser* p);

static Value jsonParseString(JSONParser* p) {
    // Opening quote already consumed or about to be
    if (!jsonMatch(p, '"')) {
        jsonError(p, "Expected '\"'");
        return NIL_VAL;
    }

    // Find the string content
    int start = p->pos;
    bool hasEscape = false;

    while (p->pos < p->length && p->source[p->pos] != '"') {
        if (p->source[p->pos] == '\\') {
            hasEscape = true;
            p->pos++; // skip escape char
        }
        p->pos++;
    }

    if (p->pos >= p->length) {
        jsonError(p, "Unterminated string");
        return NIL_VAL;
    }

    int end = p->pos;
    p->pos++; // skip closing quote

    if (!hasEscape) {
        return OBJ_VAL(copyString(p->vm, p->source + start, end - start));
    }

    // Process escape sequences
    int len = end - start;
    char* buf = (char*)malloc(len + 1);
    int out = 0;

    for (int i = start; i < end; i++) {
        if (p->source[i] == '\\' && i + 1 < end) {
            i++;
            switch (p->source[i]) {
                case '"':  buf[out++] = '"'; break;
                case '\\': buf[out++] = '\\'; break;
                case '/':  buf[out++] = '/'; break;
                case 'b':  buf[out++] = '\b'; break;
                case 'f':  buf[out++] = '\f'; break;
                case 'n':  buf[out++] = '\n'; break;
                case 'r':  buf[out++] = '\r'; break;
                case 't':  buf[out++] = '\t'; break;
                default:   buf[out++] = p->source[i]; break;
            }
        } else {
            buf[out++] = p->source[i];
        }
    }

    ObjString* str = copyString(p->vm, buf, out);
    free(buf);
    return OBJ_VAL(str);
}

static Value jsonParseNumber(JSONParser* p) {
    int start = p->pos;

    if (jsonPeek(p) == '-') p->pos++;
    while (p->pos < p->length && isdigit((unsigned char)p->source[p->pos])) p->pos++;
    if (p->pos < p->length && p->source[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->length && isdigit((unsigned char)p->source[p->pos])) p->pos++;
    }
    if (p->pos < p->length && (p->source[p->pos] == 'e' || p->source[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->length && (p->source[p->pos] == '+' || p->source[p->pos] == '-')) p->pos++;
        while (p->pos < p->length && isdigit((unsigned char)p->source[p->pos])) p->pos++;
    }

    char buf[64];
    int len = p->pos - start;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, p->source + start, len);
    buf[len] = '\0';

    return NUMBER_VAL(strtod(buf, NULL));
}

static Value jsonParseArray(JSONParser* p) {
    p->pos++; // skip '['
    ObjList* list = newList(p->vm);

    // Protect from GC
    Value listVal = OBJ_VAL(list);
    *p->vm->stackTop++ = listVal;

    jsonSkipWhitespace(p);
    if (jsonPeek(p) != ']') {
        do {
            jsonSkipWhitespace(p);
            Value element = jsonParseValue(p);
            if (p->hadError) break;
            listAppend(p->vm, list, element);
            jsonSkipWhitespace(p);
        } while (jsonMatch(p, ','));
    }

    if (!jsonMatch(p, ']')) {
        jsonError(p, "Expected ']'");
    }

    p->vm->stackTop--;
    return listVal;
}

static Value jsonParseObject(JSONParser* p) {
    p->pos++; // skip '{'
    ObjMap* map = newMap(p->vm);

    Value mapVal = OBJ_VAL(map);
    *p->vm->stackTop++ = mapVal;

    jsonSkipWhitespace(p);
    if (jsonPeek(p) != '}') {
        do {
            jsonSkipWhitespace(p);
            Value key = jsonParseString(p);
            if (p->hadError) break;
            jsonSkipWhitespace(p);
            if (!jsonMatch(p, ':')) {
                jsonError(p, "Expected ':'");
                break;
            }
            jsonSkipWhitespace(p);
            Value val = jsonParseValue(p);
            if (p->hadError) break;
            tableSet(&map->table, AS_STRING(key), val);
            jsonSkipWhitespace(p);
        } while (jsonMatch(p, ','));
    }

    if (!jsonMatch(p, '}')) {
        jsonError(p, "Expected '}'");
    }

    p->vm->stackTop--;
    return mapVal;
}

static Value jsonParseValue(JSONParser* p) {
    jsonSkipWhitespace(p);

    char c = jsonPeek(p);
    if (c == '"') return jsonParseString(p);
    if (c == '-' || isdigit((unsigned char)c)) return jsonParseNumber(p);
    if (c == '[') return jsonParseArray(p);
    if (c == '{') return jsonParseObject(p);

    if (p->pos + 4 <= p->length && memcmp(p->source + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return BOOL_VAL(true);
    }
    if (p->pos + 5 <= p->length && memcmp(p->source + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return BOOL_VAL(false);
    }
    if (p->pos + 4 <= p->length && memcmp(p->source + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return NIL_VAL;
    }

    jsonError(p, "Unexpected character");
    return NIL_VAL;
}

Value parseJSON(VM* vm, const char* json, int length) {
    JSONParser parser;
    parser.source = json;
    parser.length = length;
    parser.pos = 0;
    parser.vm = vm;
    parser.hadError = false;

    Value result = jsonParseValue(&parser);
    if (parser.hadError) return NIL_VAL;
    return result;
}

// ---- JSON Serializer ----

typedef struct {
    char* buffer;
    int length;
    int capacity;
    VM* vm;
} JSONWriter;

static void jsonWriterInit(JSONWriter* w, VM* vm) {
    w->buffer = NULL;
    w->length = 0;
    w->capacity = 0;
    w->vm = vm;
}

static void jsonWrite(JSONWriter* w, const char* str, int len) {
    if (w->length + len >= w->capacity) {
        w->capacity = (w->length + len + 1) * 2;
        w->buffer = (char*)realloc(w->buffer, w->capacity);
    }
    memcpy(w->buffer + w->length, str, len);
    w->length += len;
}

static void jsonWriteChar(JSONWriter* w, char c) {
    jsonWrite(w, &c, 1);
}

static void jsonWriteValue(JSONWriter* w, Value value);

static void jsonWriteString(JSONWriter* w, ObjString* str) {
    jsonWriteChar(w, '"');
    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        switch (c) {
            case '"':  jsonWrite(w, "\\\"", 2); break;
            case '\\': jsonWrite(w, "\\\\", 2); break;
            case '\b': jsonWrite(w, "\\b", 2); break;
            case '\f': jsonWrite(w, "\\f", 2); break;
            case '\n': jsonWrite(w, "\\n", 2); break;
            case '\r': jsonWrite(w, "\\r", 2); break;
            case '\t': jsonWrite(w, "\\t", 2); break;
            default:   jsonWriteChar(w, c); break;
        }
    }
    jsonWriteChar(w, '"');
}

static void jsonWriteValue(JSONWriter* w, Value value) {
    if (IS_NIL(value)) {
        jsonWrite(w, "null", 4);
    } else if (IS_BOOL(value)) {
        if (AS_BOOL(value)) {
            jsonWrite(w, "true", 4);
        } else {
            jsonWrite(w, "false", 5);
        }
    } else if (IS_NUMBER(value)) {
        char buf[64];
        double num = AS_NUMBER(value);
        int len;
        if (num == (int)num && num >= -1e15 && num <= 1e15) {
            len = snprintf(buf, sizeof(buf), "%d", (int)num);
        } else {
            len = snprintf(buf, sizeof(buf), "%g", num);
        }
        jsonWrite(w, buf, len);
    } else if (IS_STRING(value)) {
        jsonWriteString(w, AS_STRING(value));
    } else if (IS_LIST(value)) {
        ObjList* list = AS_LIST(value);
        jsonWriteChar(w, '[');
        for (int i = 0; i < list->count; i++) {
            if (i > 0) jsonWriteChar(w, ',');
            jsonWriteValue(w, list->items[i]);
        }
        jsonWriteChar(w, ']');
    } else if (IS_MAP(value)) {
        ObjMap* map = AS_MAP(value);
        jsonWriteChar(w, '{');
        bool first = true;
        for (int i = 0; i < map->table.capacity; i++) {
            Entry* entry = &map->table.entries[i];
            if (entry->key != NULL) {
                if (!first) jsonWriteChar(w, ',');
                first = false;
                jsonWriteString(w, entry->key);
                jsonWriteChar(w, ':');
                jsonWriteValue(w, entry->value);
            }
        }
        jsonWriteChar(w, '}');
    } else {
        jsonWrite(w, "null", 4);
    }
}

Value toJSON(VM* vm, Value value) {
    JSONWriter w;
    jsonWriterInit(&w, vm);
    jsonWriteValue(&w, value);

    if (w.buffer == NULL) {
        return OBJ_VAL(copyString(vm, "null", 4));
    }

    ObjString* result = copyString(vm, w.buffer, w.length);
    free(w.buffer);
    return OBJ_VAL(result);
}
