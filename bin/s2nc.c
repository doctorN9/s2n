/*
 * Copyright 2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <netdb.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <errno.h>

#include <s2n.h>

void usage()
{
    fprintf(stderr, "usage: s2nc [options] host [port]\n");
    fprintf(stderr, " host: hostname or IP address to connect to\n");
    fprintf(stderr, " port: port to connect to\n");
    fprintf(stderr, "\n Options:\n\n");
    fprintf(stderr, "  -a [protocols]\n");
    fprintf(stderr, "  --alpn [protocols]\n");
    fprintf(stderr, "    Sets the application protocols supported by this client, as a comma seperated list.\n");
    fprintf(stderr, "  -h,--help\n");
    fprintf(stderr, "    Display this message and quit.\n");
    fprintf(stderr, "  -n [server name]\n");
    fprintf(stderr, "  --name [server name]\n");
    fprintf(stderr, "    Sets the SNI server name header for this client.  If not specified, the host value is used.\n");
    fprintf(stderr, "  --s,--status\n");
    fprintf(stderr, "    Request the OCSP status of the remote server certificate\n");
    fprintf(stderr, "\n");
    exit(1);
}

extern int echo(struct s2n_connection *conn, int sockfd);

int main(int argc, char * const *argv)
{
    struct addrinfo hints, *ai_list, *ai;
    int r, sockfd = 0;
    /* Optional args */
    const char *alpn_protocols = NULL;
    const char *server_name = NULL;
    s2n_status_request_type type = S2N_STATUS_REQUEST_NONE;
    /* required args */
    const char *host = NULL;
    const char *port = "443";

    static struct option long_options[] = {
        { "alpn", required_argument, 0, 'a' },
        { "help", no_argument, 0, 'h' },
        { "name", required_argument, 0, 'n' },
        { "status", no_argument, 0, 's' },
    };
    while (1) {
        int option_index = 0;
        int c = getopt_long (argc, argv, "a:hns", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 'a':
                alpn_protocols = optarg;
                break;
            case 'h':
                usage();
                break;
            case 'n':
                server_name = optarg;
                break;
            case 's':
                type = S2N_STATUS_REQUEST_OCSP;
                break;
            case '?':
                usage();
                break;

            default:
                exit(1);
                break;
        }
    }

    if (optind < argc) {
        host = argv[optind++];
    }
    if (optind < argc) {
        port = argv[optind++];
    }

    if (!host) {
        usage();
    }

    if (!server_name) {
        server_name = host;
    }

    if (memset(&hints, 0, sizeof(hints)) != &hints) {
        fprintf(stderr, "memset error: %s\n", strerror(errno));
        return -1;
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((r = getaddrinfo(host, port, &hints, &ai_list)) != 0) {
        fprintf(stderr, "error: %s\n", gai_strerror(r));
        return -1;
    }

    int connected = 0;
    for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
        if ((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
            continue;
        }

        if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }

        connected = 1;
        /* connect() succeeded */
        break;
    }

    freeaddrinfo(ai_list);

    if (connected == 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", argv[1], port);
        close(sockfd);
        exit(1);
    }

    const char *error;

    if (s2n_init(&error) < 0) {
        fprintf(stderr, "Error running s2n_init(): '%s'\n", s2n_strerror(s2n_errno, "EN"));
    }

    struct s2n_config *config = s2n_config_new();
    if (config == NULL) {
        fprintf(stderr, "Error getting new config: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (s2n_config_set_status_request_type(config, type) < 0) {
        fprintf(stderr, "Error setting status request type: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (alpn_protocols) {
        char **protocols = malloc(strlen(alpn_protocols) * sizeof(char*));
        int protocol_count = 0;
        const char *ptr = alpn_protocols;
        const char *next = alpn_protocols;
        int i = 0;
        while (*ptr) {
            if (*ptr == ',') {
                protocols[protocol_count] = malloc(i + 1);
                memcpy(protocols[protocol_count], next, i);
                protocols[protocol_count][i] = 0;
                i = 0;
                protocol_count++;
                ptr++;
                next = ptr;
            } else {
                i ++;
                ptr++;
            }
        }
        if (ptr != next) {
            protocols[protocol_count] = malloc(i + 1);
            memcpy(protocols[protocol_count], next, i);
            protocols[protocol_count][i] = 0;
            protocol_count++;
        }
        if (s2n_config_set_protocol_preferences(config, (const char * const *)protocols, protocol_count) < 0) {
            fprintf(stderr, "Failed to set protocol preferences: '%s'\n", s2n_strerror(s2n_errno, "EN"));
            exit(1);
        }
        while(protocol_count) {
            protocol_count--;
            free(protocols[protocol_count]);
        }
        free(protocols);
    }

    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);

    if (conn == NULL) {
        fprintf(stderr, "Error getting new connection: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    printf("Connected to %s:%s\n", host, port);

    if (s2n_connection_set_config(conn, config) < 0) {
        fprintf(stderr, "Error setting configuration: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (s2n_set_server_name(conn, server_name) < 0) {
        fprintf(stderr, "Error setting server name: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (s2n_connection_set_fd(conn, sockfd) < 0) {
        fprintf(stderr, "Error setting file descriptor: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    /* See echo.c */
    echo(conn, sockfd);

    if (s2n_connection_free(conn) < 0) {
        fprintf(stderr, "Error freeing connection: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (s2n_config_free(config) < 0) {
        fprintf(stderr, "Error freeing configuration: '%s'\n", s2n_strerror(s2n_errno, "EN"));
        exit(1);
    }

    if (s2n_cleanup(&error) < 0) {
        fprintf(stderr, "Error running s2n_cleanup(): '%s'\n", s2n_strerror(s2n_errno, "EN"));
    }

    return 0;
}
