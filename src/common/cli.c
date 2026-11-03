#include "common/cli.h"
#include "common/types.h"

#include <arpa/inet.h>
#include <stddef.h>

bool cli_parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
                   uint32_t *value)
{
    uint32_t result = 0;

    if (text == NULL || value == NULL || *text == '\0' || minimum > maximum) {
        return false;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        uint32_t digit = (uint32_t)(*p - '0');
        if (result > maximum / 10u ||
            (result == maximum / 10u && digit > maximum % 10u)) {
            return false;
        }
        result = result * 10u + digit;
    }
    if (result < minimum) {
        return false;
    }
    *value = result;
    return true;
}

bool cli_parse_port(const char *text, uint16_t *port)
{
    uint32_t value;
    if (port == NULL || !cli_parse_u32(text, 1u, UINT16_MAX, &value)) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}

bool cli_valid_ipv4(const char *text)
{
    struct in_addr address;
    return text != NULL && inet_pton(AF_INET, text, &address) == 1;
}

bool cli_valid_username(const char *text)
{
    size_t length = 0;
    if (text == NULL) {
        return false;
    }
    for (; text[length] != '\0'; ++length) {
        char c = text[length];
        if (length >= CLUSTER_MAX_USERNAME_BYTES ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return length != 0;
}
