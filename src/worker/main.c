#include "common/cli.h"
#include "common/protocol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [<scheduler_ip> <scheduler_port>] -cores <num> -ram <MB> "
            "[-gpu <num>] [-discovery-port <port>]\n", program);
}

int main(int argc, char **argv)
{
    const char *fallback_ip = NULL;
    uint16_t fallback_port = 0;
    uint16_t discovery_port = CLUSTER_DEFAULT_DISCOVERY_PORT;
    struct resources capacity = {0};
    bool have_cores = false, have_ram = false, have_gpu = false;
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
        if (strcmp(option, "-cores") == 0 && !have_cores &&
            cli_parse_u32(value, 1u, UINT32_MAX, &capacity.cores)) {
            have_cores = true;
        } else if (strcmp(option, "-ram") == 0 && !have_ram &&
                   cli_parse_u32(value, 1u, UINT32_MAX, &capacity.ram_mb)) {
            have_ram = true;
        } else if (strcmp(option, "-gpu") == 0 && !have_gpu &&
                   cli_parse_u32(value, 0u, UINT32_MAX, &capacity.gpu)) {
            have_gpu = true;
        } else if (strcmp(option, "-discovery-port") == 0 && !have_discovery_port &&
                   cli_parse_port(value, &discovery_port)) {
            have_discovery_port = true;
        } else {
            fprintf(stderr, "Unknown, repeated, or invalid option: %s %s.\n", option, value);
            goto invalid;
        }
    }
    if (!have_cores || !have_ram) {
        fprintf(stderr, "Both -cores and -ram require positive integer values.\n");
        goto invalid;
    }

    printf("[WORKER] Requested capacity: %" PRIu32 " cores, %" PRIu32
           " MB RAM, %" PRIu32 " GPUs. Discovery port: %u.\n",
           capacity.cores, capacity.ram_mb, capacity.gpu, (unsigned)discovery_port);
    if (fallback_ip != NULL) {
        printf("[WORKER] Configured fallback: %s:%u.\n", fallback_ip, (unsigned)fallback_port);
    } else {
        puts("[WORKER] No fallback address configured.");
    }
    puts("[WORKER] Configuration validated. Capacity checks and networking are not implemented yet.");
    return EXIT_SUCCESS;

invalid:
    usage(argv[0]);
    return EXIT_FAILURE;
}
