#include "common/cli.h"
#include "common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [<scheduler_ip> <scheduler_port>] [-user <username>] "
            "[-discovery-port <port>]\n", program);
}

int main(int argc, char **argv)
{
    const char *fallback_ip = NULL;
    const char *username = NULL;
    uint16_t fallback_port = 0;
    uint16_t discovery_port = CLUSTER_DEFAULT_DISCOVERY_PORT;
    bool have_discovery_port = false;
    int i = 1;

    if (i < argc && argv[i][0] != '-') {
        if (i + 1 >= argc || !cli_valid_ipv4(argv[i]) ||
            !cli_parse_port(argv[i + 1], &fallback_port)) {
            fprintf(stderr, "Invalid fallback. Expected an IPv4 address and port 1..65535.\n");
            goto invalid;
        }
        fallback_ip = argv[i];
        i += 2;
    }
    for (; i < argc; i += 2) {
        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value for %s.\n", argv[i]);
            goto invalid;
        }
        const char *option = argv[i];
        const char *value = argv[i + 1];
        if (strcmp(option, "-user") == 0 && username == NULL && cli_valid_username(value)) {
            username = value;
        } else if (strcmp(option, "-discovery-port") == 0 && !have_discovery_port &&
                   cli_parse_port(value, &discovery_port)) {
            have_discovery_port = true;
        } else {
            fprintf(stderr, "Unknown, repeated, or invalid option: %s %s.\n", option, value);
            goto invalid;
        }
    }

    printf("[CLIENT] Configuration: discovery port %u.\n", (unsigned)discovery_port);
    if (fallback_ip != NULL) {
        printf("[CLIENT] Configured fallback: %s:%u.\n", fallback_ip, (unsigned)fallback_port);
    } else {
        puts("[CLIENT] No fallback address configured.");
    }
    if (username != NULL) {
        printf("[CLIENT] Configured username: %s.\n", username);
    } else {
        puts("[CLIENT] No username supplied; interactive login will be added later.");
    }
    puts("[CLIENT] Configuration validated. Networking is not implemented yet.");
    return EXIT_SUCCESS;

invalid:
    usage(argv[0]);
    return EXIT_FAILURE;
}
