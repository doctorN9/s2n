/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <stdio.h>

#include "error/s2n_errno.h"

#include "stuffer/s2n_stuffer.h"

#include "crypto/s2n_hmac.h"
#include "crypto/s2n_hkdf.h"
#include "crypto/s2n_tls13_keys.h"

#include "utils/s2n_blob.h"
#include "utils/s2n_safety.h"
#include "utils/s2n_mem.h"
#include "utils/s2n_safety.h"

/*
 * There are 9 keys that can be generated by the end of a TLS 1.3 handshake.
 * We currently support the following, more will be supported
 * when the relevant TLS 1.3 features are worked on.
 *
 * [x] binder_key
 * [ ] client_early_traffic_secret
 * [ ] early_exporter_master_secret
 * [x] client_handshake_traffic_secret
 * [x] server_handshake_traffic_secret
 * [x] client_application_traffic_secret_0
 * [x] server_application_traffic_secret_0
 * [ ] exporter_master_secret
 * [ ] resumption_master_secret
 *
 * The TLS 1.3 key generation can be divided into 3 phases
 * 1. early secrets
 * 2. handshake secrets
 * 3. master secrets
 *
 * In each phase, secrets are first extracted with HKDF-Extract that takes in
 * both an ikm (input keying material) and a salt. Some keys can be derived/expanded
 * from the extract before a "tls13 derived" Derive-Secret is used to
 * derive the input salt for the next phase.
 */

/*
 * Define TLS 1.3 HKDF labels as specified in
 * https://tools.ietf.org/html/rfc8446#section-7.1
 */
S2N_BLOB_LABEL(s2n_tls13_label_derived_secret, "derived")

S2N_BLOB_LABEL(s2n_tls13_label_external_psk_binder_key, "ext binder")
S2N_BLOB_LABEL(s2n_tls13_label_resumption_psk_binder_key, "res binder")

S2N_BLOB_LABEL(s2n_tls13_label_client_early_traffic_secret, "c e traffic")
S2N_BLOB_LABEL(s2n_tls13_label_early_exporter_master_secret, "e exp master")

S2N_BLOB_LABEL(s2n_tls13_label_client_handshake_traffic_secret, "c hs traffic")
S2N_BLOB_LABEL(s2n_tls13_label_server_handshake_traffic_secret, "s hs traffic")

S2N_BLOB_LABEL(s2n_tls13_label_client_application_traffic_secret, "c ap traffic")
S2N_BLOB_LABEL(s2n_tls13_label_server_application_traffic_secret, "s ap traffic")

S2N_BLOB_LABEL(s2n_tls13_label_exporter_master_secret, "exp master")
S2N_BLOB_LABEL(s2n_tls13_label_resumption_master_secret, "res master")

/*
 * Traffic secret labels
 */
S2N_BLOB_LABEL(s2n_tls13_label_traffic_secret_key, "key")
S2N_BLOB_LABEL(s2n_tls13_label_traffic_secret_iv, "iv")

/*
 * TLS 1.3 Finished label
 */
S2N_BLOB_LABEL(s2n_tls13_label_finished, "finished")

/*
 * TLS 1.3 KeyUpdate label
 */
S2N_BLOB_LABEL(s2n_tls13_label_application_traffic_secret_update, "traffic upd")

static const struct s2n_blob zero_length_blob = { .data = NULL, .size = 0 };

/* Message transcript hash based on selected HMAC algorithm */
static int s2n_tls13_transcript_message_hash(struct s2n_tls13_keys *keys, const struct s2n_blob *message, struct s2n_blob *message_digest)
{
    notnull_check(keys);
    notnull_check(message);
    notnull_check(message_digest);

    DEFER_CLEANUP(struct s2n_hash_state hash_state, s2n_hash_free);
    GUARD(s2n_hash_new(&hash_state));
    GUARD(s2n_hash_init(&hash_state, keys->hash_algorithm));
    GUARD(s2n_hash_update(&hash_state, message->data, message->size));
    GUARD(s2n_hash_digest(&hash_state, message_digest->data, message_digest->size));

    return 0;
}

/*
 * Initializes the tls13_keys struct
 */
int s2n_tls13_keys_init(struct s2n_tls13_keys *keys, s2n_hmac_algorithm alg)
{
    notnull_check(keys);

    keys->hmac_algorithm = alg;
    GUARD(s2n_hmac_hash_alg(alg, &keys->hash_algorithm));
    GUARD(s2n_hash_digest_size(keys->hash_algorithm, &keys->size));
    GUARD(s2n_blob_init(&keys->extract_secret, keys->extract_secret_bytes, keys->size));
    GUARD(s2n_blob_init(&keys->derive_secret, keys->derive_secret_bytes, keys->size));
    GUARD(s2n_hmac_new(&keys->hmac));

    return 0;
}

/*
 * Frees any allocation
 */
int s2n_tls13_keys_free(struct s2n_tls13_keys *keys) {
    notnull_check(keys);

    GUARD(s2n_hmac_free(&keys->hmac));

    return 0;
}

/*
 * Derives binder_key from PSK.
 */
int s2n_tls13_derive_binder_key(struct s2n_tls13_keys *keys, struct s2n_psk *psk)
{
    notnull_check(keys);
    notnull_check(psk);

    struct s2n_blob *early_secret = &keys->extract_secret;
    struct s2n_blob *binder_key = &keys->derive_secret;

    /* Extract the early secret */
    GUARD(s2n_hkdf_extract(&keys->hmac, keys->hmac_algorithm, &zero_length_blob,
            &psk->secret, early_secret));

    /* Choose the correct label for the psk type */
    const struct s2n_blob *label_blob;
    if (psk->type == S2N_PSK_TYPE_EXTERNAL) {
        label_blob = &s2n_tls13_label_external_psk_binder_key;
    } else {
        label_blob = &s2n_tls13_label_resumption_psk_binder_key;
    }

    /* Derive the binder_key */
    s2n_tls13_key_blob(message_digest, keys->size);
    GUARD(s2n_tls13_transcript_message_hash(keys, &zero_length_blob, &message_digest));
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, early_secret,
        label_blob, &message_digest, binder_key));

    return S2N_SUCCESS;
}

/*
 * Derives early secrets
 */
int s2n_tls13_derive_early_secrets(struct s2n_tls13_keys *keys)
{
    notnull_check(keys);

    s2n_tls13_key_blob(psk_ikm, keys->size); /* in 1-RTT, PSK is 0-filled of key length */

    /* Early Secret */
    GUARD(s2n_hkdf_extract(&keys->hmac, keys->hmac_algorithm, &zero_length_blob, &psk_ikm, &keys->extract_secret));

    /* client_early_traffic_secret and early_exporter_master_secret can be derived here */

    /* derive next secret */
    s2n_tls13_key_blob(message_digest, keys->size);
    GUARD(s2n_tls13_transcript_message_hash(keys, &zero_length_blob, &message_digest));
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, &keys->extract_secret,
        &s2n_tls13_label_derived_secret, &message_digest, &keys->derive_secret));

    return 0;
}

/*
 * Derives handshake secrets
 */
int s2n_tls13_derive_handshake_secrets(struct s2n_tls13_keys *keys,
                                        const struct s2n_blob *ecdhe,
                                        struct s2n_hash_state *client_server_hello_hash,
                                        struct s2n_blob *client_secret,
                                        struct s2n_blob *server_secret)
{
    notnull_check(keys);
    notnull_check(ecdhe);
    notnull_check(client_server_hello_hash);
    notnull_check(client_secret);
    notnull_check(server_secret);

    /* Handshake Secret */
    GUARD(s2n_hkdf_extract(&keys->hmac, keys->hmac_algorithm, &keys->derive_secret, ecdhe, &keys->extract_secret));

    s2n_tls13_key_blob(message_digest, keys->size);

    /* copy the hash */
    DEFER_CLEANUP(struct s2n_hash_state hkdf_hash_copy, s2n_hash_free);
    GUARD(s2n_hash_new(&hkdf_hash_copy));
    GUARD(s2n_hash_copy(&hkdf_hash_copy, client_server_hello_hash));
    s2n_hash_digest(&hkdf_hash_copy, message_digest.data, message_digest.size);

    /* produce client + server traffic secrets */
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, &keys->extract_secret,
        &s2n_tls13_label_client_handshake_traffic_secret, &message_digest, client_secret));
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, &keys->extract_secret,
        &s2n_tls13_label_server_handshake_traffic_secret, &message_digest, server_secret));

    /* derive next secret */
    GUARD(s2n_tls13_transcript_message_hash(keys, &zero_length_blob, &message_digest));
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, &keys->extract_secret,
        &s2n_tls13_label_derived_secret, &message_digest, &keys->derive_secret));

    return 0;
}

int s2n_tls13_extract_master_secret(struct s2n_tls13_keys *keys)
{
    s2n_tls13_key_blob(empty_key, keys->size);

    /* Extract master secret from derived secret */
    GUARD(s2n_hkdf_extract(&keys->hmac, keys->hmac_algorithm, &keys->derive_secret, &empty_key, &keys->extract_secret));

    return S2N_SUCCESS;
}

int s2n_tls13_derive_application_secret(struct s2n_tls13_keys *keys, struct s2n_hash_state *hashes, struct s2n_blob *secret_blob, s2n_mode mode)
{
    notnull_check(keys);
    notnull_check(hashes);
    notnull_check(secret_blob);

    const struct s2n_blob *label_blob;
    if (mode == S2N_CLIENT) {
        label_blob = &s2n_tls13_label_client_application_traffic_secret;
    } else {
        label_blob = &s2n_tls13_label_server_application_traffic_secret;
    }

    /* Sanity check that input hash is of expected type */
    S2N_ERROR_IF(keys->hash_algorithm != hashes->alg, S2N_ERR_HASH_INVALID_ALGORITHM);

    s2n_tls13_key_blob(message_digest, keys->size);

    /* copy the hashes into the message_digest */
    DEFER_CLEANUP(struct s2n_hash_state hkdf_hash_copy, s2n_hash_free);
    GUARD(s2n_hash_new(&hkdf_hash_copy));
    GUARD(s2n_hash_copy(&hkdf_hash_copy, hashes));
    GUARD(s2n_hash_digest(&hkdf_hash_copy, message_digest.data, message_digest.size));

    /* Derive traffic secret from master secret */
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, &keys->extract_secret,
        label_blob, &message_digest, secret_blob));

    return S2N_SUCCESS;
}

/*
 * Derive Traffic Key and IV based on input secret
 */
int s2n_tls13_derive_traffic_keys(struct s2n_tls13_keys *keys, struct s2n_blob *secret, struct s2n_blob *key, struct s2n_blob *iv)
{
    notnull_check(keys);
    notnull_check(secret);
    notnull_check(key);
    notnull_check(iv);

    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, secret,
        &s2n_tls13_label_traffic_secret_key, &zero_length_blob, key));
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, secret,
        &s2n_tls13_label_traffic_secret_iv, &zero_length_blob, iv));
    return 0;
}

/*
 * Generate finished key for compute finished hashes/MACs
 * https://tools.ietf.org/html/rfc8446#section-4.4.4
 */
int s2n_tls13_derive_finished_key(struct s2n_tls13_keys *keys, struct s2n_blob *secret_key, struct s2n_blob *output_finish_key)
{
    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, secret_key, &s2n_tls13_label_finished, &zero_length_blob, output_finish_key));

    return 0;
}

/*
 * Compute finished verify data using HMAC
 * with a finished key and hash state
 * https://tools.ietf.org/html/rfc8446#section-4.4.4
 */
int s2n_tls13_calculate_finished_mac(struct s2n_tls13_keys *keys, struct s2n_blob *finished_key, struct s2n_hash_state *hash_state, struct s2n_blob *finished_verify)
{
    /* Set up a blob to contain hash */
    s2n_tls13_key_blob(transcript_hash, keys->size);

    /* Make a copy of the hash state */
    DEFER_CLEANUP(struct s2n_hash_state hash_state_copy, s2n_hash_free);
    GUARD(s2n_hash_new(&hash_state_copy));
    GUARD(s2n_hash_copy(&hash_state_copy, hash_state));
    GUARD(s2n_hash_digest(&hash_state_copy, transcript_hash.data, transcript_hash.size));

    GUARD(s2n_hkdf_extract(&keys->hmac, keys->hmac_algorithm, finished_key, &transcript_hash, finished_verify));

    return 0;
}

/*
 * Derives next generation of traffic secret
 */
int s2n_tls13_update_application_traffic_secret(struct s2n_tls13_keys *keys, struct s2n_blob *old_secret, struct s2n_blob *new_secret)
{
    notnull_check(keys);
    notnull_check(old_secret);
    notnull_check(new_secret);

    GUARD(s2n_hkdf_expand_label(&keys->hmac, keys->hmac_algorithm, old_secret,
                                &s2n_tls13_label_application_traffic_secret_update, &zero_length_blob, new_secret));

    return 0;
}
