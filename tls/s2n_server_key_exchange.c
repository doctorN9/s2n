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

#include <s2n.h>

#include "error/s2n_errno.h"

#include "tls/s2n_tls_digest_preferences.h"
#include "tls/s2n_cipher_suites.h"
#include "tls/s2n_connection.h"
#include "tls/s2n_signature_algorithms.h"

#include "stuffer/s2n_stuffer.h"

#include "crypto/s2n_dhe.h"

#include "utils/s2n_safety.h"
#include "utils/s2n_random.h"

static int s2n_write_signature_blob(struct s2n_stuffer *out, const struct s2n_pkey *priv_key, struct s2n_hash_state *digest);

int s2n_server_key_recv(struct s2n_connection *conn)
{
    struct s2n_hash_state *signature_hash = &conn->secure.signature_hash;
    const struct s2n_key_exchange_algorithm *key_exchange = conn->secure.cipher_suite->key_exchange_alg;
    struct s2n_stuffer *in = &conn->handshake.io;
    struct s2n_blob data_to_sign = {0};

    /* Read and process the KEX data */
    notnull_check(key_exchange->server_key_recv);
    GUARD(key_exchange->server_key_recv(conn, &data_to_sign));

    /* Add common signature data */
    if (conn->actual_protocol_version == S2N_TLS12) {
        s2n_hash_algorithm hash_algorithm;
        s2n_signature_algorithm signature_algorithm;
        GUARD(s2n_get_signature_hash_pair_if_supported(in, &hash_algorithm, &signature_algorithm));
        GUARD(s2n_hash_init(signature_hash, hash_algorithm));
    } else {
        GUARD(s2n_hash_init(signature_hash, conn->secure.conn_hash_alg));
    }
    GUARD(s2n_hash_update(signature_hash, conn->secure.client_random, S2N_TLS_RANDOM_DATA_LEN));
    GUARD(s2n_hash_update(signature_hash, conn->secure.server_random, S2N_TLS_RANDOM_DATA_LEN));

    /* Add KEX specific data */
    GUARD(s2n_hash_update(signature_hash, data_to_sign.data, data_to_sign.size));

    /* Verify the signature */
    struct s2n_blob signature = {0};
    uint16_t signature_length;
    GUARD(s2n_stuffer_read_uint16(in, &signature_length));
    signature.size = signature_length;
    signature.data = s2n_stuffer_raw_read(in, signature.size);
    notnull_check(signature.data);
    gt_check(signature_length, 0);

    S2N_ERROR_IF(s2n_pkey_verify(&conn->secure.server_public_key, signature_hash, &signature) < 0, S2N_ERR_BAD_MESSAGE);

    /* We don't need the key any more, so free it */
    GUARD(s2n_pkey_free(&conn->secure.server_public_key));
    return 0;
}

int s2n_ecdhe_server_recv_params(struct s2n_connection *conn, struct s2n_blob *data_to_verify)
{
    struct s2n_stuffer *in = &conn->handshake.io;

    /* Read server ECDH params and calculate their hash */
    GUARD(s2n_ecc_read_ecc_params(&conn->secure.server_ecc_params, in, data_to_verify));
    return 0;
}

int s2n_dhe_server_recv_params(struct s2n_connection *conn, struct s2n_blob *data_to_verify)
{
    struct s2n_stuffer *in = &conn->handshake.io;
    struct s2n_blob p, g, Ys;
    uint16_t p_length;
    uint16_t g_length;
    uint16_t Ys_length;

    /* Keep a copy to the start of the whole structure for the signature check */
    data_to_verify->data = s2n_stuffer_raw_read(in, 0);
    notnull_check(data_to_verify->data);

    /* Read each of the three elements in */
    GUARD(s2n_stuffer_read_uint16(in, &p_length));
    p.size = p_length;
    p.data = s2n_stuffer_raw_read(in, p.size);
    notnull_check(p.data);

    GUARD(s2n_stuffer_read_uint16(in, &g_length));
    g.size = g_length;
    g.data = s2n_stuffer_raw_read(in, g.size);
    notnull_check(g.data);

    GUARD(s2n_stuffer_read_uint16(in, &Ys_length));
    Ys.size = Ys_length;
    Ys.data = s2n_stuffer_raw_read(in, Ys.size);
    notnull_check(Ys.data);

    /* Now we know the total size of the structure */
    data_to_verify->size = 2 + p_length + 2 + g_length + 2 + Ys_length;

    /* Copy the DH details */
    GUARD(s2n_dh_p_g_Ys_to_dh_params(&conn->secure.server_dh_params, &p, &g, &Ys));
    return 0;
}

int s2n_server_key_send(struct s2n_connection *conn)
{
    struct s2n_hash_state *signature_hash = &conn->secure.signature_hash;
    const struct s2n_key_exchange_algorithm *key_exchange = conn->secure.cipher_suite->key_exchange_alg;
    struct s2n_stuffer *out = &conn->handshake.io;
    struct s2n_blob data_to_sign = {0};

    /* Call the negotiated key exchange method to send it's data */
    notnull_check(key_exchange->server_key_send);
    GUARD(key_exchange->server_key_send(conn, &data_to_sign));

    /* Add common signature data */
    if (conn->actual_protocol_version == S2N_TLS12) {
        GUARD(s2n_stuffer_write_uint8(out, s2n_hash_alg_to_tls[ conn->secure.conn_hash_alg ]));
        GUARD(s2n_stuffer_write_uint8(out, conn->secure.conn_sig_alg));
    }

    /* Add the random data to the hash */
    GUARD(s2n_hash_init(signature_hash, conn->secure.conn_hash_alg));
    GUARD(s2n_hash_update(signature_hash, conn->secure.client_random, S2N_TLS_RANDOM_DATA_LEN));
    GUARD(s2n_hash_update(signature_hash, conn->secure.server_random, S2N_TLS_RANDOM_DATA_LEN));

    /* Add KEX specific data to the hash */
    GUARD(s2n_hash_update(signature_hash, data_to_sign.data, data_to_sign.size));

    /* Sign and write the signature */
    GUARD(s2n_write_signature_blob(out, &conn->config->cert_and_key_pairs->private_key, signature_hash));
    return 0;
}

int s2n_ecdhe_server_send_params(struct s2n_connection *conn, struct s2n_blob *data_to_sign)
{
    struct s2n_stuffer *out = &conn->handshake.io;

    /* Generate an ephemeral key and  */
    GUARD(s2n_ecc_generate_ephemeral_key(&conn->secure.server_ecc_params));

    /* Write it out and calculate the data to sign later */
    GUARD(s2n_ecc_write_ecc_params(&conn->secure.server_ecc_params, out, data_to_sign));
    return 0;
}

int s2n_dhe_server_send_params(struct s2n_connection *conn, struct s2n_blob *data_to_sign)
{
    struct s2n_stuffer *out = &conn->handshake.io;

    /* Duplicate the DH key from the config */
    GUARD(s2n_dh_params_copy(conn->config->dhparams, &conn->secure.server_dh_params));

    /* Generate an ephemeral key */
    GUARD(s2n_dh_generate_ephemeral_key(&conn->secure.server_dh_params));

    /* Write it out and calculate the data to sign later */
    GUARD(s2n_dh_params_to_p_g_Ys(&conn->secure.server_dh_params, out, data_to_sign));
    return 0;
}

static int s2n_write_signature_blob(struct s2n_stuffer *out, const struct s2n_pkey *priv_key, struct s2n_hash_state *digest)
{
    struct s2n_blob signature = {0};
    
    /* Leave signature length blank for now until we're done signing */
    uint16_t sig_len = 0;
    GUARD(s2n_stuffer_write_uint16(out, sig_len));
    
    int max_signature_size = s2n_pkey_size(priv_key);
    signature.size = max_signature_size;
    signature.data = s2n_stuffer_raw_write(out, signature.size);
    notnull_check(signature.data);

    S2N_ERROR_IF(s2n_pkey_sign(priv_key, digest, &signature) < 0, S2N_ERR_DH_FAILED_SIGNING);

    /* Now that the signature has been created, write the actual size that was stored in the signature blob */
    out->write_cursor -= max_signature_size;
    out->write_cursor -= 2;

    GUARD(s2n_stuffer_write_uint16(out, signature.size));
    GUARD(s2n_stuffer_skip_write(out, signature.size));
    return 0;
}

// The client should never receive an additional RSA key during RSA key exchange
int s2n_rsa_server_recv_key(struct s2n_connection *conn, struct s2n_blob *data_to_verify)
{
    S2N_ERROR(S2N_ERR_HANDSHAKE_STATE);
}
// The server should never send an additional RSA key during RSA key exchange
int s2n_rsa_server_send_key(struct s2n_connection *conn, struct s2n_blob *data_to_sign)
{
    S2N_ERROR(S2N_ERR_HANDSHAKE_STATE);
}
