#ifndef CLUSTER_CLI_H
#define CLUSTER_CLI_H

#include <stdbool.h>
#include <stdint.h>

/* Return false on invalid input. Output parameters change only on success. */
bool cli_parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
                   uint32_t *value);
bool cli_parse_port(const char *text, uint16_t *port);
bool cli_valid_ipv4(const char *text);
bool cli_valid_username(const char *text);

#endif
