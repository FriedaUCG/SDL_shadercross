/*
  Simple DirectMedia Layer Shader Cross Compiler
  Copyright (C) 2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "cli.h"

#include <SDL3/SDL_iostream.h>

#define SDL_SHADERCROSS_CLI_MANIFEST_SCHEMA "sdl-shadercross-cli-manifest"
#define SDL_SHADERCROSS_CLI_MANIFEST_VERSION 1
#define SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES (1024 * 1024)
#define SDL_SHADERCROSS_CLI_MANIFEST_MAX_DEPTH 64

typedef enum JsonType {
    JSON_NULL,
    JSON_BOOL,
    JSON_INTEGER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonMember JsonMember;

typedef struct JsonValue {
    JsonType type;
    int line;
    int column;
    union {
        bool boolean;
        Sint64 integer;
        char *string;
        struct {
            struct JsonValue *items;
            size_t count;
        } array;
        struct {
            JsonMember *members;
            size_t count;
        } object;
    } data;
} JsonValue;

struct JsonMember {
    char *key;
    JsonValue value;
};

typedef struct JsonParser {
    const char *text;
    size_t length;
    size_t position;
    int line;
    int column;
} JsonParser;

static void json_free_value(JsonValue *value);

static bool json_set_error_at(const JsonParser *parser, const char *message)
{
    return SDL_SetError("manifest JSON parse error at line %d column %d: %s", parser->line, parser->column, message);
}

static bool json_set_value_error(const JsonValue *value, const char *context, const char *message)
{
    return SDL_SetError("manifest %s at line %d column %d: %s", context, value->line, value->column, message);
}

static char json_peek(const JsonParser *parser)
{
    return parser->position < parser->length ? parser->text[parser->position] : '\0';
}

static char json_advance(JsonParser *parser)
{
    char c = json_peek(parser);
    if (c != '\0') {
        parser->position += 1;
        if (c == '\n') {
            parser->line += 1;
            parser->column = 1;
        } else {
            parser->column += 1;
        }
    }
    return c;
}

static void json_skip_whitespace(JsonParser *parser)
{
    for (;;) {
        switch (json_peek(parser)) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            json_advance(parser);
            break;
        default:
            return;
        }
    }
}

static bool json_append_byte(char **buffer, size_t *length, size_t *capacity, char c)
{
    if (*length + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        char *new_buffer = (char *)SDL_realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return SDL_OutOfMemory();
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }
    (*buffer)[*length] = c;
    *length += 1;
    (*buffer)[*length] = '\0';
    return true;
}

static bool json_append_utf8(char **buffer, size_t *length, size_t *capacity, Uint32 codepoint)
{
    if (codepoint == 0) {
        return SDL_SetError("NUL Unicode escape is not allowed in manifest strings");
    }
    if (codepoint <= 0x7f) {
        return json_append_byte(buffer, length, capacity, (char)codepoint);
    }
    if (codepoint <= 0x7ff) {
        return json_append_byte(buffer, length, capacity, (char)(0xc0 | (codepoint >> 6))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0xffff) {
        return json_append_byte(buffer, length, capacity, (char)(0xe0 | (codepoint >> 12))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0x10ffff) {
        return json_append_byte(buffer, length, capacity, (char)(0xf0 | (codepoint >> 18))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | ((codepoint >> 12) & 0x3f))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               json_append_byte(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    }
    return SDL_SetError("invalid Unicode code point");
}

static int json_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    return -1;
}

static bool json_parse_hex4(JsonParser *parser, Uint32 *codepoint)
{
    Uint32 value = 0;

    for (int i = 0; i < 4; i += 1) {
        const int digit = json_hex_value(json_peek(parser));
        if (digit < 0) {
            return json_set_error_at(parser, "expected four hexadecimal digits after \\u");
        }
        value = (value << 4) | (Uint32)digit;
        json_advance(parser);
    }
    *codepoint = value;
    return true;
}

static bool json_parse_unicode_escape(JsonParser *parser, Uint32 *codepoint)
{
    Uint32 first = 0;

    if (!json_parse_hex4(parser, &first)) {
        return false;
    }
    if (first >= 0xd800 && first <= 0xdbff) {
        Uint32 second = 0;
        if (json_advance(parser) != '\\' || json_advance(parser) != 'u') {
            return json_set_error_at(parser, "high surrogate must be followed by a low surrogate escape");
        }
        if (!json_parse_hex4(parser, &second)) {
            return false;
        }
        if (second < 0xdc00 || second > 0xdfff) {
            return json_set_error_at(parser, "high surrogate must be followed by a low surrogate");
        }
        *codepoint = 0x10000 + (((first - 0xd800) << 10) | (second - 0xdc00));
        return true;
    }
    if (first >= 0xdc00 && first <= 0xdfff) {
        return json_set_error_at(parser, "low surrogate without preceding high surrogate");
    }
    *codepoint = first;
    return true;
}

static bool json_parse_string(JsonParser *parser, JsonValue *value)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    value->type = JSON_STRING;
    value->line = parser->line;
    value->column = parser->column;

    if (json_advance(parser) != '"') {
        return json_set_error_at(parser, "expected string");
    }

    while (parser->position < parser->length) {
        char c = json_advance(parser);
        if (c == '"') {
            if (buffer == NULL) {
                buffer = SDL_strdup("");
                if (buffer == NULL) {
                    return SDL_OutOfMemory();
                }
            }
            value->data.string = buffer;
            return true;
        }
        if ((unsigned char)c < 0x20) {
            SDL_free(buffer);
            return json_set_error_at(parser, "control character in string");
        }
        if (c == '\\') {
            Uint32 codepoint = 0;
            c = json_advance(parser);
            switch (c) {
            case '"':
            case '\\':
            case '/':
                if (!json_append_byte(&buffer, &length, &capacity, c)) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 'b':
                if (!json_append_byte(&buffer, &length, &capacity, '\b')) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 'f':
                if (!json_append_byte(&buffer, &length, &capacity, '\f')) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 'n':
                if (!json_append_byte(&buffer, &length, &capacity, '\n')) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 'r':
                if (!json_append_byte(&buffer, &length, &capacity, '\r')) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 't':
                if (!json_append_byte(&buffer, &length, &capacity, '\t')) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            case 'u':
                if (!json_parse_unicode_escape(parser, &codepoint) ||
                    !json_append_utf8(&buffer, &length, &capacity, codepoint)) {
                    SDL_free(buffer);
                    return false;
                }
                break;
            default:
                SDL_free(buffer);
                return json_set_error_at(parser, "invalid string escape");
            }
        } else if (!json_append_byte(&buffer, &length, &capacity, c)) {
            SDL_free(buffer);
            return false;
        }
    }

    SDL_free(buffer);
    return json_set_error_at(parser, "unterminated string");
}

static bool json_parse_integer(JsonParser *parser, JsonValue *value)
{
    bool negative = false;
    Uint64 parsed = 0;
    bool have_digit = false;

    value->type = JSON_INTEGER;
    value->line = parser->line;
    value->column = parser->column;

    if (json_peek(parser) == '-') {
        negative = true;
        json_advance(parser);
    }

    if (json_peek(parser) == '0') {
        have_digit = true;
        json_advance(parser);
        if (json_peek(parser) >= '0' && json_peek(parser) <= '9') {
            return json_set_error_at(parser, "leading zero in integer");
        }
    } else {
        while (json_peek(parser) >= '0' && json_peek(parser) <= '9') {
            const Uint64 digit = (Uint64)(json_advance(parser) - '0');
            have_digit = true;
            if (parsed > (SDL_MAX_SINT64 - digit) / 10) {
                return json_set_error_at(parser, "integer out of range");
            }
            parsed = parsed * 10 + digit;
        }
    }

    if (!have_digit) {
        return json_set_error_at(parser, "expected digit");
    }
    if (json_peek(parser) == '.' || json_peek(parser) == 'e' || json_peek(parser) == 'E') {
        return json_set_error_at(parser, "floating-point numbers are not accepted in shadercross manifests");
    }
    if (negative) {
        if (parsed > (Uint64)SDL_MAX_SINT64 + 1u) {
            return json_set_error_at(parser, "integer out of range");
        }
        value->data.integer = parsed == (Uint64)SDL_MAX_SINT64 + 1u ? SDL_MIN_SINT64 : -(Sint64)parsed;
    } else {
        value->data.integer = (Sint64)parsed;
    }
    return true;
}

static bool json_parse_value(JsonParser *parser, JsonValue *value, int depth);

static JsonMember *json_object_find(JsonValue *object, const char *key)
{
    for (size_t i = 0; i < object->data.object.count; i += 1) {
        if (SDL_strcmp(object->data.object.members[i].key, key) == 0) {
            return &object->data.object.members[i];
        }
    }
    return NULL;
}

static bool json_parse_object(JsonParser *parser, JsonValue *value, int depth)
{
    value->type = JSON_OBJECT;
    value->line = parser->line;
    value->column = parser->column;
    value->data.object.members = NULL;
    value->data.object.count = 0;

    json_advance(parser);
    json_skip_whitespace(parser);
    if (json_peek(parser) == '}') {
        json_advance(parser);
        return true;
    }

    for (;;) {
        JsonValue key;
        JsonValue member_value;
        JsonMember *members = NULL;
        SDL_zero(key);
        SDL_zero(member_value);

        if (json_peek(parser) != '"') {
            return json_set_error_at(parser, "expected object key string");
        }
        if (!json_parse_string(parser, &key)) {
            return false;
        }
        if (json_object_find(value, key.data.string) != NULL) {
            char duplicate[256];
            SDL_snprintf(duplicate, sizeof(duplicate), "duplicate object key '%s'", key.data.string);
            json_free_value(&key);
            return json_set_error_at(parser, duplicate);
        }
        json_skip_whitespace(parser);
        if (json_advance(parser) != ':') {
            json_free_value(&key);
            return json_set_error_at(parser, "expected ':' after object key");
        }
        json_skip_whitespace(parser);
        if (!json_parse_value(parser, &member_value, depth + 1)) {
            json_free_value(&key);
            json_free_value(&member_value);
            return false;
        }

        members = (JsonMember *)SDL_realloc(
            value->data.object.members,
            sizeof(*value->data.object.members) * (value->data.object.count + 1));
        if (members == NULL) {
            json_free_value(&key);
            json_free_value(&member_value);
            return SDL_OutOfMemory();
        }
        value->data.object.members = members;
        value->data.object.members[value->data.object.count].key = key.data.string;
        value->data.object.members[value->data.object.count].value = member_value;
        value->data.object.count += 1;

        json_skip_whitespace(parser);
        if (json_peek(parser) == '}') {
            json_advance(parser);
            return true;
        }
        if (json_advance(parser) != ',') {
            json_free_value(value);
            return json_set_error_at(parser, "expected ',' or '}' after object member");
        }
        json_skip_whitespace(parser);
    }
}

static bool json_parse_array(JsonParser *parser, JsonValue *value, int depth)
{
    value->type = JSON_ARRAY;
    value->line = parser->line;
    value->column = parser->column;
    value->data.array.items = NULL;
    value->data.array.count = 0;

    json_advance(parser);
    json_skip_whitespace(parser);
    if (json_peek(parser) == ']') {
        json_advance(parser);
        return true;
    }

    for (;;) {
        JsonValue item;
        JsonValue *items = NULL;
        SDL_zero(item);

        if (!json_parse_value(parser, &item, depth + 1)) {
            json_free_value(&item);
            return false;
        }
        items = (JsonValue *)SDL_realloc(
            value->data.array.items,
            sizeof(*value->data.array.items) * (value->data.array.count + 1));
        if (items == NULL) {
            json_free_value(&item);
            return SDL_OutOfMemory();
        }
        value->data.array.items = items;
        value->data.array.items[value->data.array.count] = item;
        value->data.array.count += 1;

        json_skip_whitespace(parser);
        if (json_peek(parser) == ']') {
            json_advance(parser);
            return true;
        }
        if (json_advance(parser) != ',') {
            json_free_value(value);
            return json_set_error_at(parser, "expected ',' or ']' after array item");
        }
        json_skip_whitespace(parser);
    }
}

static bool json_parse_literal(JsonParser *parser, JsonValue *value, const char *literal, JsonType type, bool boolean)
{
    value->type = type;
    value->line = parser->line;
    value->column = parser->column;
    value->data.boolean = boolean;

    for (size_t i = 0; literal[i] != '\0'; i += 1) {
        if (json_advance(parser) != literal[i]) {
            return json_set_error_at(parser, "invalid literal");
        }
    }
    return true;
}

static bool json_parse_value(JsonParser *parser, JsonValue *value, int depth)
{
    SDL_zero(*value);

    if (depth > SDL_SHADERCROSS_CLI_MANIFEST_MAX_DEPTH) {
        return json_set_error_at(parser, "maximum JSON nesting depth exceeded");
    }

    json_skip_whitespace(parser);
    switch (json_peek(parser)) {
    case '{':
        return json_parse_object(parser, value, depth);
    case '[':
        return json_parse_array(parser, value, depth);
    case '"':
        return json_parse_string(parser, value);
    case 't':
        return json_parse_literal(parser, value, "true", JSON_BOOL, true);
    case 'f':
        return json_parse_literal(parser, value, "false", JSON_BOOL, false);
    case 'n':
        return json_parse_literal(parser, value, "null", JSON_NULL, false);
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return json_parse_integer(parser, value);
    default:
        return json_set_error_at(parser, "expected JSON value");
    }
}

static void json_free_value(JsonValue *value)
{
    switch (value->type) {
    case JSON_STRING:
        SDL_free(value->data.string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < value->data.array.count; i += 1) {
            json_free_value(&value->data.array.items[i]);
        }
        SDL_free(value->data.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < value->data.object.count; i += 1) {
            SDL_free(value->data.object.members[i].key);
            json_free_value(&value->data.object.members[i].value);
        }
        SDL_free(value->data.object.members);
        break;
    default:
        break;
    }
    SDL_zero(*value);
}

static bool json_parse_document(const char *text, size_t length, JsonValue *root)
{
    JsonParser parser;

    SDL_zero(parser);
    parser.text = text;
    parser.length = length;
    parser.line = 1;
    parser.column = 1;

    if (!json_parse_value(&parser, root, 0)) {
        json_free_value(root);
        return false;
    }
    json_skip_whitespace(&parser);
    if (parser.position != parser.length) {
        json_free_value(root);
        return json_set_error_at(&parser, "unexpected trailing data");
    }
    return true;
}

static JsonMember *manifest_get_member(JsonValue *object, const char *key)
{
    if (object->type != JSON_OBJECT) {
        return NULL;
    }
    return json_object_find(object, key);
}

static bool manifest_reject_unknown_keys(JsonValue *object, const char *context, const char *const *allowed_keys, size_t num_allowed_keys)
{
    if (object->type != JSON_OBJECT) {
        return json_set_value_error(object, context, "expected object");
    }

    for (size_t i = 0; i < object->data.object.count; i += 1) {
        bool known = false;
        for (size_t j = 0; j < num_allowed_keys; j += 1) {
            if (SDL_strcmp(object->data.object.members[i].key, allowed_keys[j]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            return SDL_SetError("manifest %s: unknown key '%s'", context, object->data.object.members[i].key);
        }
    }
    return true;
}

static JsonValue *manifest_required(JsonValue *object, const char *key, JsonType type, const char *context)
{
    JsonMember *member = manifest_get_member(object, key);
    if (member == NULL) {
        SDL_SetError("manifest %s: missing required key '%s'", context, key);
        return NULL;
    }
    if (member->value.type != type) {
        SDL_SetError("manifest %s.%s: expected %s", context, key,
            type == JSON_STRING ? "string" :
            type == JSON_INTEGER ? "integer" :
            type == JSON_ARRAY ? "array" :
            type == JSON_OBJECT ? "object" :
            type == JSON_BOOL ? "boolean" : "null");
        return NULL;
    }
    return &member->value;
}

static bool manifest_optional_value(JsonValue *object, const char *key, JsonType type, const char *context, JsonValue **value)
{
    JsonMember *member = manifest_get_member(object, key);
    *value = NULL;
    if (member == NULL) {
        return true;
    }
    if (member->value.type != type) {
        SDL_SetError("manifest %s.%s: expected %s", context, key,
            type == JSON_STRING ? "string" :
            type == JSON_INTEGER ? "integer" :
            type == JSON_ARRAY ? "array" :
            type == JSON_OBJECT ? "object" :
            type == JSON_BOOL ? "boolean" : "null");
        return false;
    }
    *value = &member->value;
    return true;
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#ifdef SDL_PLATFORM_WINDOWS
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}

static bool make_manifest_dir(const char *manifest_path, char **dir)
{
    const char *slash = SDL_strrchr(manifest_path, '/');
#ifdef SDL_PLATFORM_WINDOWS
    const char *backslash = SDL_strrchr(manifest_path, '\\');
#endif
    const char *separator = slash;
    size_t length = 0;

#ifdef SDL_PLATFORM_WINDOWS
    if (backslash != NULL && (separator == NULL || backslash > separator)) {
        separator = backslash;
    }
#endif
    if (separator == NULL) {
        *dir = SDL_strdup("");
    } else {
        length = (size_t)(separator - manifest_path) + 1;
        *dir = (char *)SDL_malloc(length + 1);
        if (*dir != NULL) {
            SDL_memcpy(*dir, manifest_path, length);
            (*dir)[length] = '\0';
        }
    }
    if (*dir == NULL) {
        return SDL_OutOfMemory();
    }
    return true;
}

static bool load_manifest_text(const char *manifest_path, char **text, size_t *data_size)
{
    SDL_IOStream *stream = NULL;
    char *buffer = NULL;
    size_t capacity = 0;
    size_t size = 0;
    Sint64 stream_size = -1;
    bool result = false;

    *text = NULL;
    *data_size = 0;

    stream = SDL_IOFromFile(manifest_path, "rb");
    if (stream == NULL) {
        return SDL_SetError("failed to open manifest '%s': %s", manifest_path, SDL_GetError());
    }

    stream_size = SDL_GetIOSize(stream);
    if (stream_size > SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES) {
        SDL_SetError("manifest '%s' is too large; maximum is %d bytes", manifest_path, SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES);
        goto done;
    }
    capacity = stream_size >= 0 ? (size_t)stream_size + 1 : 4096;
    buffer = (char *)SDL_malloc(capacity > 0 ? capacity : 1);
    if (buffer == NULL) {
        SDL_OutOfMemory();
        goto done;
    }

    for (;;) {
        size_t read_size = 4096;
        size_t bytes_read = 0;

        if (stream_size >= 0) {
            const size_t remaining = (size_t)stream_size - size;
            if (remaining == 0) {
                break;
            }
            if (read_size > remaining) {
                read_size = remaining;
            }
        } else if (read_size > SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES + 1 - size) {
            read_size = SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES + 1 - size;
        }

        if (size + read_size + 1 > capacity) {
            size_t new_capacity = capacity * 2;
            char *new_buffer = NULL;
            if (new_capacity < size + read_size + 1) {
                new_capacity = size + read_size + 1;
            }
            new_buffer = (char *)SDL_realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                SDL_OutOfMemory();
                goto done;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        bytes_read = SDL_ReadIO(stream, buffer + size, read_size);
        if (bytes_read == 0) {
            const SDL_IOStatus status = SDL_GetIOStatus(stream);
            if (status == SDL_IO_STATUS_EOF) {
                break;
            }
            SDL_SetError("failed to read manifest '%s': %s", manifest_path, SDL_GetError());
            goto done;
        }
        size += bytes_read;
        if (size > SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES) {
            SDL_SetError("manifest '%s' is too large; maximum is %d bytes", manifest_path, SDL_SHADERCROSS_CLI_MANIFEST_MAX_BYTES);
            goto done;
        }
    }

    buffer[size] = '\0';
    *text = buffer;
    *data_size = size;
    buffer = NULL;
    result = true;

done:
    SDL_free(buffer);
    SDL_CloseIO(stream);
    return result;
}

static bool resolve_manifest_path(const char *manifest_dir, const char *path, char **resolved_path)
{
    if (path == NULL || path[0] == '\0') {
        return SDL_SetError("manifest path values must not be empty");
    }
    if (path_is_absolute(path) || manifest_dir[0] == '\0') {
        return ShaderCross_CLIOptions_SetString(resolved_path, path);
    }
    SDL_free(*resolved_path);
    *resolved_path = NULL;
    if (SDL_asprintf(resolved_path, "%s%s", manifest_dir, path) < 0) {
        return SDL_OutOfMemory();
    }
    return true;
}

static bool load_source(JsonValue *root, const char *manifest_dir, ShaderCross_CLIOptions *options)
{
    static const char *const source_keys[] = { "path", "format" };
    JsonValue *source = manifest_required(root, "source", JSON_OBJECT, "root");
    JsonValue *path = NULL;
    JsonValue *format = NULL;

    if (source == NULL ||
        !manifest_reject_unknown_keys(source, "source", source_keys, SDL_arraysize(source_keys))) {
        return false;
    }
    path = manifest_required(source, "path", JSON_STRING, "source");
    format = manifest_required(source, "format", JSON_STRING, "source");
    if (path == NULL || format == NULL) {
        return false;
    }
    if (!resolve_manifest_path(manifest_dir, path->data.string, &options->filename)) {
        return false;
    }
    return ShaderCross_CLIOptions_SetSourceFormat(options, format->data.string);
}

static bool load_includes(JsonValue *root, const char *manifest_dir, ShaderCross_CLIOptions *options)
{
    JsonValue *includes = NULL;

    if (!manifest_optional_value(root, "includes", JSON_ARRAY, "root", &includes)) {
        return false;
    }
    if (includes == NULL) {
        return true;
    }
    if (includes->data.array.count > 1) {
        return json_set_value_error(includes, "includes", "only one include directory is supported by the current SDL_shadercross CLI/API");
    }
    if (includes->data.array.count == 1) {
        JsonValue *include = &includes->data.array.items[0];
        if (include->type != JSON_STRING) {
            return json_set_value_error(include, "includes[]", "expected string");
        }
        if (!resolve_manifest_path(manifest_dir, include->data.string, &options->include_dir)) {
            return false;
        }
    }
    return true;
}

static bool load_defines(JsonValue *root, ShaderCross_CLIOptions *options)
{
    static const char *const define_keys[] = { "name", "value" };
    JsonValue *defines = NULL;

    if (!manifest_optional_value(root, "defines", JSON_ARRAY, "root", &defines)) {
        return false;
    }
    if (defines == NULL) {
        return true;
    }
    for (size_t i = 0; i < defines->data.array.count; i += 1) {
        JsonValue *define = &defines->data.array.items[i];
        JsonValue *name = NULL;
        JsonValue *value = NULL;
        if (define->type != JSON_OBJECT) {
            return json_set_value_error(define, "defines[]", "expected object");
        }
        if (!manifest_reject_unknown_keys(define, "defines[]", define_keys, SDL_arraysize(define_keys))) {
            return false;
        }
        name = manifest_required(define, "name", JSON_STRING, "defines[]");
        if (!manifest_optional_value(define, "value", JSON_STRING, "defines[]", &value) || name == NULL) {
            return false;
        }
        if (!ShaderCross_CLIOptions_AddDefine(options, name->data.string, value ? value->data.string : NULL)) {
            return false;
        }
    }
    return true;
}

static bool load_options(JsonValue *root, ShaderCross_CLIOptions *options)
{
    static const char *const option_keys[] = { "cull_unused_bindings", "debug", "msl_version", "pssl" };
    JsonValue *option_object = NULL;
    JsonValue *value = NULL;

    if (!manifest_optional_value(root, "options", JSON_OBJECT, "root", &option_object)) {
        return false;
    }
    if (option_object == NULL) {
        return true;
    }
    if (!manifest_reject_unknown_keys(option_object, "options", option_keys, SDL_arraysize(option_keys))) {
        return false;
    }
    if (!manifest_optional_value(option_object, "cull_unused_bindings", JSON_BOOL, "options", &value)) {
        return false;
    }
    if (value != NULL) {
        options->cull_unused_bindings = value->data.boolean;
    }
    if (!manifest_optional_value(option_object, "debug", JSON_BOOL, "options", &value)) {
        return false;
    }
    if (value != NULL) {
        options->enable_debug = value->data.boolean;
    }
    if (!manifest_optional_value(option_object, "pssl", JSON_BOOL, "options", &value)) {
        return false;
    }
    if (value != NULL) {
        options->pssl_compat = value->data.boolean;
    }
    if (!manifest_optional_value(option_object, "msl_version", JSON_STRING, "options", &value)) {
        return false;
    }
    if (value != NULL && !ShaderCross_CLIOptions_SetString(&options->msl_version, value->data.string)) {
        return false;
    }
    return true;
}

static bool load_targets(JsonValue *root, const char *manifest_dir, ShaderCross_CLIOptions *options)
{
    static const char *const target_keys[] = { "kind", "format", "path", "symbol_prefix" };
    JsonValue *targets = manifest_required(root, "targets", JSON_ARRAY, "root");
    bool have_layout_target = false;

    if (targets == NULL) {
        return false;
    }
    if (targets->data.array.count == 0) {
        return json_set_value_error(targets, "targets", "at least one target is required");
    }
    for (size_t i = 0; i < targets->data.array.count; i += 1) {
        JsonValue *target = &targets->data.array.items[i];
        JsonValue *kind = NULL;
        JsonValue *path = NULL;
        JsonValue *format = NULL;
        JsonValue *symbol_prefix = NULL;

        if (target->type != JSON_OBJECT) {
            return json_set_value_error(target, "targets[]", "expected object");
        }
        if (!manifest_reject_unknown_keys(target, "targets[]", target_keys, SDL_arraysize(target_keys))) {
            return false;
        }
        kind = manifest_required(target, "kind", JSON_STRING, "targets[]");
        if (kind == NULL) {
            return false;
        }
        if (SDL_strcmp(kind->data.string, "shader") == 0) {
            char *resolved_path = NULL;
            ShaderCross_ShaderFormat shader_format = SHADERFORMAT_INVALID;
            bool added = false;

            path = manifest_required(target, "path", JSON_STRING, "targets[]");
            format = manifest_required(target, "format", JSON_STRING, "targets[]");
            if (!manifest_optional_value(target, "symbol_prefix", JSON_STRING, "targets[]", &symbol_prefix) ||
                path == NULL || format == NULL) {
                return false;
            }
            if (symbol_prefix != NULL) {
                return json_set_value_error(symbol_prefix, "targets[].symbol_prefix", "shader targets must not specify symbol_prefix");
            }
            if (!ShaderCross_CLIOptions_ParseDestinationFormat(format->data.string, &shader_format) ||
                !resolve_manifest_path(manifest_dir, path->data.string, &resolved_path)) {
                return false;
            }
            added = ShaderCross_CLIOptions_AddShaderTarget(options, shader_format, resolved_path);
            SDL_free(resolved_path);
            if (!added) {
                return false;
            }
        } else if (SDL_strcmp(kind->data.string, "resource_layout_c") == 0) {
            if (have_layout_target) {
                return json_set_value_error(target, "targets[]", "only one resource_layout_c target is supported in this manifest slice");
            }
            have_layout_target = true;
            path = manifest_required(target, "path", JSON_STRING, "targets[]");
            symbol_prefix = manifest_required(target, "symbol_prefix", JSON_STRING, "targets[]");
            if (!manifest_optional_value(target, "format", JSON_STRING, "targets[]", &format) ||
                path == NULL || symbol_prefix == NULL) {
                return false;
            }
            if (format != NULL) {
                return json_set_value_error(format, "targets[].format", "resource_layout_c targets must not specify format");
            }
            if (!resolve_manifest_path(manifest_dir, path->data.string, &options->resource_layout_filename) ||
                !ShaderCross_CLIOptions_SetString(&options->resource_layout_symbol_prefix, symbol_prefix->data.string)) {
                return false;
            }
        } else {
            return json_set_value_error(kind, "targets[].kind", "expected 'shader' or 'resource_layout_c'");
        }
    }
    return true;
}

static bool load_policies(JsonValue *root, ShaderCross_CLIOptions *options)
{
    static const char *const policy_keys[] = { "sampled_slots", "storage_texture_slots" };
    JsonValue *policies = NULL;
    JsonValue *slots = NULL;

    if (!manifest_optional_value(root, "policies", JSON_OBJECT, "root", &policies)) {
        return false;
    }
    if (policies == NULL) {
        return true;
    }
    if (!manifest_reject_unknown_keys(policies, "policies", policy_keys, SDL_arraysize(policy_keys))) {
        return false;
    }
    if (!manifest_optional_value(policies, "sampled_slots", JSON_ARRAY, "policies", &slots)) {
        return false;
    }
    if (slots != NULL) {
        for (size_t i = 0; i < slots->data.array.count; i += 1) {
            JsonValue *slot = &slots->data.array.items[i];
            if (slot->type != JSON_STRING) {
                return json_set_value_error(slot, "policies.sampled_slots[]", "expected string");
            }
            if (!ShaderCross_CLIOptions_AppendSampledSlotPolicy(options, slot->data.string, "--resource-layout-sampled-slot")) {
                return false;
            }
        }
    }
    if (!manifest_optional_value(policies, "storage_texture_slots", JSON_ARRAY, "policies", &slots)) {
        return false;
    }
    if (slots != NULL) {
        for (size_t i = 0; i < slots->data.array.count; i += 1) {
            JsonValue *slot = &slots->data.array.items[i];
            if (slot->type != JSON_STRING) {
                return json_set_value_error(slot, "policies.storage_texture_slots[]", "expected string");
            }
            if (!ShaderCross_CLIOptions_AppendStorageTextureSlotPolicy(options, slot->data.string)) {
                return false;
            }
        }
    }
    return true;
}

static bool load_manifest_root(JsonValue *root, const char *manifest_dir, ShaderCross_CLIOptions *options)
{
    static const char *const root_keys[] = {
        "schema",
        "version",
        "source",
        "stage",
        "entrypoint",
        "includes",
        "defines",
        "options",
        "targets",
        "policies"
    };
    JsonValue *schema = NULL;
    JsonValue *version = NULL;
    JsonValue *stage = NULL;
    JsonValue *entrypoint = NULL;

    if (!manifest_reject_unknown_keys(root, "root", root_keys, SDL_arraysize(root_keys))) {
        return false;
    }
    schema = manifest_required(root, "schema", JSON_STRING, "root");
    version = manifest_required(root, "version", JSON_INTEGER, "root");
    stage = manifest_required(root, "stage", JSON_STRING, "root");
    if (schema == NULL || version == NULL || stage == NULL) {
        return false;
    }
    if (SDL_strcmp(schema->data.string, SDL_SHADERCROSS_CLI_MANIFEST_SCHEMA) != 0) {
        return json_set_value_error(schema, "schema", "expected '" SDL_SHADERCROSS_CLI_MANIFEST_SCHEMA "'");
    }
    if (version->data.integer != SDL_SHADERCROSS_CLI_MANIFEST_VERSION) {
        return json_set_value_error(version, "version", "unsupported manifest version");
    }
    if (!load_source(root, manifest_dir, options) ||
        !ShaderCross_CLIOptions_SetStage(options, stage->data.string)) {
        return false;
    }
    if (!manifest_optional_value(root, "entrypoint", JSON_STRING, "root", &entrypoint)) {
        return false;
    }
    if (entrypoint != NULL && !ShaderCross_CLIOptions_SetString(&options->entrypoint_name, entrypoint->data.string)) {
        return false;
    }
    return load_includes(root, manifest_dir, options) &&
           load_defines(root, options) &&
           load_options(root, options) &&
           load_targets(root, manifest_dir, options) &&
           load_policies(root, options);
}

bool ShaderCross_CLI_LoadManifest(const char *manifest_path, ShaderCross_CLIOptions *options)
{
    char *text = NULL;
    char *manifest_dir = NULL;
    size_t data_size = 0;
    JsonValue root;
    bool result = false;

    SDL_zero(root);
    if (!load_manifest_text(manifest_path, &text, &data_size)) {
        return false;
    }
    if (!json_parse_document(text, data_size, &root)) {
        goto done;
    }
    if (!make_manifest_dir(manifest_path, &manifest_dir)) {
        goto done;
    }
    result = load_manifest_root(&root, manifest_dir, options);

done:
    json_free_value(&root);
    SDL_free(manifest_dir);
    SDL_free(text);
    return result;
}
