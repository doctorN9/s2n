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
#include "tests/s2n_test.h"

#include "tls/s2n_kex.h"
#include "tls/s2n_kem.h"
#include "tls/s2n_tls_parameters.h"
#include "tls/s2n_cipher_preferences.h"

#include "utils/s2n_safety.h"

#define TEST_PUBLIC_KEY_LENGTH  2
const char TEST_PUBLIC_KEY[] = {2, 2};
#define TEST_PRIVATE_KEY_LENGTH  3
const char TEST_PRIVATE_KEY[] = {3, 3, 3};
#define TEST_SHARED_SECRET_LENGTH  4
const char TEST_SHARED_SECRET[] = {4, 4, 4, 4};
#define TEST_CIPHERTEXT_LENGTH  5
const char TEST_CIPHERTEXT[] = {5, 5, 5, 5, 5};


int s2n_test_generate_keypair(unsigned char *public_key, unsigned char *private_key)
{
    memset(public_key, TEST_PUBLIC_KEY_LENGTH, TEST_PUBLIC_KEY_LENGTH);
    memset(private_key, TEST_PRIVATE_KEY_LENGTH, TEST_PRIVATE_KEY_LENGTH);
    return 0;
}
int s2n_test_encrypt(unsigned char *ciphertext, unsigned char *shared_secret, const unsigned char *public_key)
{
    GUARD(memcmp(public_key, TEST_PUBLIC_KEY, TEST_PUBLIC_KEY_LENGTH));
    memset(ciphertext, TEST_CIPHERTEXT_LENGTH, TEST_CIPHERTEXT_LENGTH);
    memset(shared_secret, TEST_SHARED_SECRET_LENGTH, TEST_SHARED_SECRET_LENGTH);
    return 0;
}
int s2n_test_decrypt(unsigned char *shared_secret, const unsigned char *ciphertext, const unsigned char *private_key)
{
    GUARD(memcmp(ciphertext, TEST_CIPHERTEXT, TEST_CIPHERTEXT_LENGTH));
    GUARD(memcmp(private_key, TEST_PRIVATE_KEY, TEST_PRIVATE_KEY_LENGTH));
    memset(shared_secret, TEST_SHARED_SECRET_LENGTH, TEST_SHARED_SECRET_LENGTH);
    return 0;
}

const struct s2n_kem s2n_test_kem = {
        .public_key_length = TEST_PUBLIC_KEY_LENGTH,
        .private_key_length = TEST_PRIVATE_KEY_LENGTH,
        .shared_secret_key_length = TEST_SHARED_SECRET_LENGTH,
        .ciphertext_length = TEST_CIPHERTEXT_LENGTH,
        .generate_keypair = &s2n_test_generate_keypair,
        .encapsulate = &s2n_test_encrypt,
        .decapsulate = &s2n_test_decrypt,
};

int main(int argc, char **argv)
{
    BEGIN_TEST();
    {
        /* Regression test for network parsing data of expected sizes */
        EXPECT_EQUAL(sizeof(kem_extension_size), 2);
        EXPECT_EQUAL(sizeof(kem_public_key_size), 2);
        EXPECT_EQUAL(sizeof(kem_private_key_size), 2);
        EXPECT_EQUAL(sizeof(kem_shared_secret_size), 2);
        EXPECT_EQUAL(sizeof(kem_ciphertext_key_size), 2);
    }
    {
        struct s2n_kem_keypair server_kem_keypair = {0};
        server_kem_keypair.negotiated_kem = &s2n_test_kem;
        EXPECT_SUCCESS(s2n_alloc(&server_kem_keypair.public_key, TEST_PUBLIC_KEY_LENGTH));
        EXPECT_SUCCESS(s2n_kem_generate_keypair(&server_kem_keypair));
        EXPECT_EQUAL(TEST_PUBLIC_KEY_LENGTH, server_kem_keypair.public_key.size);
        EXPECT_EQUAL(TEST_PRIVATE_KEY_LENGTH, server_kem_keypair.private_key.size);
        EXPECT_BYTEARRAY_EQUAL(TEST_PUBLIC_KEY, server_kem_keypair.public_key.data, TEST_PUBLIC_KEY_LENGTH);
        EXPECT_BYTEARRAY_EQUAL(TEST_PRIVATE_KEY, server_kem_keypair.private_key.data, TEST_PRIVATE_KEY_LENGTH);

        struct s2n_kem_keypair client_kem_keypair = {0};
        client_kem_keypair.negotiated_kem = &s2n_test_kem;
        /* This would be handled by client/server key exchange methods which isn't being tested */
        GUARD(s2n_alloc(&client_kem_keypair.public_key, TEST_PUBLIC_KEY_LENGTH));
        memset(client_kem_keypair.public_key.data, TEST_PUBLIC_KEY_LENGTH, TEST_PUBLIC_KEY_LENGTH);

        DEFER_CLEANUP(struct s2n_blob client_shared_secret = {0}, s2n_free);
        DEFER_CLEANUP(struct s2n_blob ciphertext = {0}, s2n_free);
        GUARD(s2n_alloc(&ciphertext, TEST_CIPHERTEXT_LENGTH));

        EXPECT_SUCCESS(s2n_kem_encapsulate(&client_kem_keypair, &client_shared_secret, &ciphertext));
        EXPECT_EQUAL(TEST_SHARED_SECRET_LENGTH, client_shared_secret.size);
        EXPECT_EQUAL(TEST_CIPHERTEXT_LENGTH, ciphertext.size);
        EXPECT_BYTEARRAY_EQUAL(TEST_SHARED_SECRET, client_shared_secret.data, TEST_SHARED_SECRET_LENGTH);
        EXPECT_BYTEARRAY_EQUAL(TEST_CIPHERTEXT, ciphertext.data, TEST_CIPHERTEXT_LENGTH);

        DEFER_CLEANUP(struct s2n_blob server_shared_secret = {0}, s2n_free);
        EXPECT_SUCCESS(s2n_kem_decapsulate(&server_kem_keypair, &server_shared_secret, &ciphertext));
        EXPECT_EQUAL(TEST_SHARED_SECRET_LENGTH, server_shared_secret.size);
        EXPECT_BYTEARRAY_EQUAL(TEST_SHARED_SECRET, server_shared_secret.data, TEST_SHARED_SECRET_LENGTH);

        EXPECT_SUCCESS(s2n_kem_free(&server_kem_keypair));
        EXPECT_SUCCESS(s2n_kem_free(&client_kem_keypair));
    }
    {
        /* The order of the client kem list should always be ignored; the server chooses based on the
         * order of the server preference list, as long as the client claims to support it. */
        const struct s2n_kem *negotiated_kem = NULL;
        uint8_t bike_iana[S2N_TLS_CIPHER_SUITE_LEN] = { TLS_ECDHE_BIKE_RSA_WITH_AES_256_GCM_SHA384 };
        uint8_t sike_iana[S2N_TLS_CIPHER_SUITE_LEN] = { TLS_ECDHE_SIKE_RSA_WITH_AES_256_GCM_SHA384 };
        {

            /* bike1l1r1, bike1l1r2, sikep503r1, sikep434r2 */
            uint8_t client_kems[] = {0x00, 0x01, 0x00, 0x0d, 0x00, 0x0a, 0x00, 0x10};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 8));

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R2);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            /* sikep503r1, bike1l1r1, sikep434r2, bike1l1r2 */
            uint8_t client_kems[] = {0x00, 0x0a, 0x00, 0x01, 0x00, 0x10, 0x00, 0x0d};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 8));

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R2);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            /* sikep503r1, bike1l1r1 */
            uint8_t client_kems[] = {0x00, 0x0a, 0x00, 0x01};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 4));

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;
        }
        {
            /* bike1l1r2, sikep434r2 */
            uint8_t client_kems[] = {0x00, 0x0d, 0x00, 0x10};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 4));

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R2);
            negotiated_kem = NULL;

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            /* bike1l1r1, sikep434r2 */
            uint8_t client_kems[] = {0x00, 0x01, 0x00, 0x10};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 4));

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            /* bike1l1r1, bike1l1r2 */
            uint8_t client_kems[] = {0x00, 0x01, 0x00, 0x0d};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 4));

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R2);
            negotiated_kem = NULL;

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);
        }
        {
            /* sikep434r2, sikep503r1 */
            uint8_t client_kems[] = {0x00, 0x10, 0x00, 0x0a};
            struct s2n_blob client_kem_blob = {0};
            EXPECT_SUCCESS(s2n_blob_init(&client_kem_blob, client_kems, 4));

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_find_supported_kem(bike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_find_supported_kem(sike_iana, &client_kem_blob, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            /* If the client sends no KEMs, the server chooses whichever one it prefers. */
            EXPECT_SUCCESS(s2n_kem_choose_server_preferred_kem(bike_iana, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_choose_server_preferred_kem(bike_iana, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_BIKE1_L1_R2);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_choose_server_preferred_kem(sike_iana, pq_kems_r1, 2, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P503_R1);
            negotiated_kem = NULL;

            EXPECT_SUCCESS(s2n_kem_choose_server_preferred_kem(sike_iana, pq_kems_r1r2, 4, &negotiated_kem));
            EXPECT_NOT_NULL(negotiated_kem);
            EXPECT_EQUAL(negotiated_kem->kem_extension_id, TLS_PQ_KEM_EXTENSION_ID_SIKE_P434_R2);
            negotiated_kem = NULL;
        }
        {
            const struct s2n_kem *sike_only_server_pref_list[] = {
                    &s2n_sike_p434_r2,
                    &s2n_sike_p503_r1
            };

            const struct s2n_kem *bike_r2_only_server_pref_list[] = {
                    &s2n_bike1_l1_r2
            };

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_choose_server_preferred_kem(bike_iana, sike_only_server_pref_list, 2, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);

            EXPECT_FAILURE_WITH_ERRNO(s2n_kem_choose_server_preferred_kem(sike_iana, bike_r2_only_server_pref_list, 1, &negotiated_kem), S2N_ERR_KEM_UNSUPPORTED_PARAMS);
            EXPECT_NULL(negotiated_kem);
        }
    }
    {
        const struct s2n_iana_to_kem *compatible_params = NULL;
        const uint8_t classic_ecdhe[S2N_TLS_CIPHER_SUITE_LEN] = {TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA};
        EXPECT_FAILURE(s2n_cipher_suite_to_kem(classic_ecdhe, &compatible_params));
        EXPECT_NULL(compatible_params);

        compatible_params = NULL;
        const uint8_t bike_iana[S2N_TLS_CIPHER_SUITE_LEN] = {TLS_ECDHE_BIKE_RSA_WITH_AES_256_GCM_SHA384};
        EXPECT_SUCCESS(s2n_cipher_suite_to_kem(bike_iana, &compatible_params));
        EXPECT_EQUAL(compatible_params->kem_count, 2);
        EXPECT_EQUAL(compatible_params->kems[0]->kem_extension_id, s2n_bike1_l1_r1.kem_extension_id);
        EXPECT_EQUAL(compatible_params->kems[1]->kem_extension_id, s2n_bike1_l1_r2.kem_extension_id);

        compatible_params = NULL;
        const uint8_t sike_iana[S2N_TLS_CIPHER_SUITE_LEN] = {TLS_ECDHE_SIKE_RSA_WITH_AES_256_GCM_SHA384};
        EXPECT_SUCCESS(s2n_cipher_suite_to_kem(sike_iana, &compatible_params));
        EXPECT_EQUAL(compatible_params->kem_count, 2);
        EXPECT_EQUAL(compatible_params->kems[0]->kem_extension_id, s2n_sike_p503_r1.kem_extension_id);
        EXPECT_EQUAL(compatible_params->kems[1]->kem_extension_id, s2n_sike_p434_r2.kem_extension_id);
    }

    END_TEST();
}
