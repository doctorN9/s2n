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

#include <stdint.h>
#include <string.h>

#include "error/s2n_errno.h"

#include "tls/s2n_tls_parameters.h"
#include "tls/s2n_connection.h"

#include "stuffer/s2n_stuffer.h"

#include "utils/s2n_safety.h"
#include "utils/s2n_blob.h"

int s2n_server_extensions_send(struct s2n_connection *conn, struct s2n_stuffer *out)
{
    uint16_t total_size = 0;

    uint8_t application_protocol_len = strlen(conn->application_protocol);
    if (application_protocol_len) {
        total_size += 7 + application_protocol_len;
    }

    if (total_size == 0) {
        return 0;
    }

    GUARD(s2n_stuffer_write_uint16(out, total_size));

    /* Write ALPN extension */
    if (application_protocol_len) {
        GUARD(s2n_stuffer_write_uint16(out, TLS_EXTENSION_ALPN));
        GUARD(s2n_stuffer_write_uint16(out, application_protocol_len + 3));
        GUARD(s2n_stuffer_write_uint16(out, application_protocol_len + 1));
        GUARD(s2n_stuffer_write_uint8(out, application_protocol_len));
        GUARD(s2n_stuffer_write_bytes(out, (uint8_t*)conn->application_protocol, application_protocol_len));
    }

    return 0;
}

int s2n_server_extensions_recv(struct s2n_connection *conn, struct s2n_blob *extensions)
{
    struct s2n_stuffer in;

    GUARD(s2n_stuffer_init(&in, extensions));
    GUARD(s2n_stuffer_write(&in, extensions));

    while (s2n_stuffer_data_available(&in)) {
        struct s2n_blob ext;
        uint16_t extension_type, extension_size;
        struct s2n_stuffer extension;

        GUARD(s2n_stuffer_read_uint16(&in, &extension_type));
        GUARD(s2n_stuffer_read_uint16(&in, &extension_size));

        ext.size = extension_size;
        ext.data = s2n_stuffer_raw_read(&in, ext.size);
        notnull_check(ext.data);

        GUARD(s2n_stuffer_init(&extension, &ext));
        GUARD(s2n_stuffer_write(&extension, &ext));

        switch (extension_type) {
            uint16_t size_of_all;

        case TLS_EXTENSION_ALPN:
            GUARD(s2n_stuffer_read_uint16(&extension, &size_of_all));
            if (size_of_all > s2n_stuffer_data_available(&extension) || size_of_all < 3) {
                continue;
            }

            uint8_t protocol_len;
            GUARD(s2n_stuffer_read_uint8(&extension, &protocol_len));
            if (protocol_len > sizeof(conn->application_protocol) - 1) {
                continue;
            }

            uint8_t *protocol = s2n_stuffer_raw_read(&extension, protocol_len);
            notnull_check(protocol);

            /* copy the first protocol name */
            memcpy_check(conn->application_protocol, protocol, protocol_len);
            protocol[protocol_len] = '\0';
            break;
        }
    }

    return 0;
}

