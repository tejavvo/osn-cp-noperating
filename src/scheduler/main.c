#include "common/cli.h"
#include "common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <port> <FIFO|PRIORITY> [-discovery-port <port>]\n",
            program);
}

int main(int argc, char **argv)
{
    uint16_t port;
    uint16_t discovery_port = CLUSTER_DEFAULT_DISCOVERY_PORT;
    enum scheduling_algorithm algorithm;

    if (argc != 3 && argc != 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!cli_parse_port(argv[1], &port)) {
        fprintf(stderr, "Invalid TCP port. Expected 1..65535.\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[2], "FIFO") == 0) {
        algorithm = SCHEDULING_FIFO;
    } else if (strcmp(argv[2], "PRIORITY") == 0) {
        algorithm = SCHEDULING_PRIORITY;
    } else {
        fprintf(stderr, "Invalid scheduling algorithm. Expected FIFO or PRIORITY.\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 5 && (strcmp(argv[3], "-discovery-port") != 0 ||
                     !cli_parse_port(argv[4], &discovery_port))) {
        fprintf(stderr, "Invalid discovery option. Expected -discovery-port <1..65535>.\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("[SCHEDULER] Configuration: TCP port %u, algorithm %s, discovery port %u.\n",
           (unsigned)port, algorithm == SCHEDULING_FIFO ? "FIFO" : "PRIORITY",
           (unsigned)discovery_port);
    puts("[SCHEDULER] Configuration validated. Networking is not implemented yet.");
    return EXIT_SUCCESS;
}
