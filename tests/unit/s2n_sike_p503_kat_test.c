/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 *
 * Modified from PQCgenKAT_kem.c
 * Created by Bassham, Lawrence E (Fed) on 8/29/17.
 * Copyright © 2017 Bassham, Lawrence E (Fed). All rights reserved.
 */

#include "s2n_test.h"
#include "crypto/s2n_drbg.h"
#include "pq-crypto/sike/sike_p503_kem.h"
#include "pq-crypto/pq-random.h"
#include "tests/unit/s2n_nist_kats.h"
#include "utils/s2n_mem.h"
#include "utils/s2n_safety.h"

#define RSP_FILE_NAME "kats/sike_p503.kat"

struct s2n_blob kat_entropy_blob = {0};

int kat_entropy(struct s2n_blob *blob)
{
    eq_check(blob->size, kat_entropy_blob.size);
    blob->data = kat_entropy_blob.data;
    return 0;
}

struct s2n_drbg kat_drbg = {0};

static int kat_get_random_bytes(struct s2n_blob *blob)
{
    GUARD(s2n_drbg_generate(&kat_drbg, blob));
    return 0;
}

int main(int argc, char **argv, char **envp) {
    BEGIN_TEST();

    // Flip pq-random over to nist RNG to ensure KAT values match
    EXPECT_SUCCESS(initialize_pq_crypto_generator(&kat_get_random_bytes));
    /* SIKE knwon answers were generated with an AES_256_CTR_NO_DF_NO_PR DRBG */
    EXPECT_SUCCESS(s2n_drbg_enable_dangerous_modes());

    FILE *kat_file = fopen(RSP_FILE_NAME, "r");
    EXPECT_NOT_NULL(kat_file);

    int count;
    EXPECT_SUCCESS(s2n_alloc(&kat_entropy_blob, 48));

    // Client side variables
    uint8_t ct[SIKE_P503_CIPHERTEXT_BYTES];
    uint8_t client_shared_secret[SIKE_P503_SHARED_SECRET_BYTES];

    // Server side variables
    uint8_t pk[SIKE_P503_PUBLIC_KEY_BYTES];
    uint8_t sk[SIKE_P503_SECRET_KEY_BYTES];
    uint8_t server_shared_secret[SIKE_P503_SHARED_SECRET_BYTES];

    // Known answer variables
    uint8_t pk_answer[SIKE_P503_PUBLIC_KEY_BYTES];
    uint8_t sk_answer[SIKE_P503_SECRET_KEY_BYTES];
    uint8_t ct_answer[SIKE_P503_CIPHERTEXT_BYTES];
    uint8_t shared_secret_answer[SIKE_P503_SHARED_SECRET_BYTES];

    s2n_stack_blob(persoanlization_string, 48, 48);

    for (uint32_t i = 0; i < NUM_OF_KATS; i++) {
        // Verify test index
        EXPECT_SUCCESS(FindMarker(kat_file, "count = "));
        EXPECT_TRUE(fscanf(kat_file, "%d", &count) > 0);
        EXPECT_EQUAL(count, i);

        // Set the NIST rng to the same state the response file was created with
        EXPECT_SUCCESS(ReadHex(kat_file, kat_entropy_blob.data, 48, "seed = "));
        kat_drbg.entropy_generator = &kat_entropy;
        EXPECT_SUCCESS(s2n_drbg_instantiate(&kat_drbg, &persoanlization_string, S2N_DANGEROUS_AES_256_CTR_NO_DF_NO_PR));

        ////////////////////////////////////
        //      Run the prtocol
        ////////////////////////////////////

        // Generate the public/private key pair
        EXPECT_SUCCESS(SIKE_P503_crypto_kem_keypair(pk, sk));

        // Create a shared secret and use the public key to encrypt it
        EXPECT_SUCCESS(SIKE_P503_crypto_kem_enc(ct, client_shared_secret, pk));

        // Use the private key to decrypt the ct to get the shared secret
        EXPECT_SUCCESS(SIKE_P503_crypto_kem_dec(server_shared_secret, ct, sk));

        ////////////////////////////////////
        //      Verify the results
        ////////////////////////////////////

        // Read the KAT values
        EXPECT_SUCCESS(ReadHex(kat_file, pk_answer, SIKE_P503_PUBLIC_KEY_BYTES, "pk = "));
        EXPECT_SUCCESS(ReadHex(kat_file, sk_answer, SIKE_P503_SECRET_KEY_BYTES, "sk = "));
        EXPECT_SUCCESS(ReadHex(kat_file, ct_answer, SIKE_P503_CIPHERTEXT_BYTES, "ct = "));
        EXPECT_SUCCESS(ReadHex(kat_file, shared_secret_answer, SIKE_P503_SHARED_SECRET_BYTES, "ss = "));

        // Test the client and server got the same value
        EXPECT_BYTEARRAY_EQUAL(client_shared_secret, server_shared_secret, SIKE_P503_SHARED_SECRET_BYTES);

        // Compare the KAT values
        EXPECT_BYTEARRAY_EQUAL(pk_answer, pk, SIKE_P503_PUBLIC_KEY_BYTES);
        EXPECT_BYTEARRAY_EQUAL(sk_answer, sk, SIKE_P503_SECRET_KEY_BYTES);
        EXPECT_BYTEARRAY_EQUAL(ct_answer, ct, SIKE_P503_CIPHERTEXT_BYTES);
        EXPECT_BYTEARRAY_EQUAL(shared_secret_answer, server_shared_secret, SIKE_P503_SHARED_SECRET_BYTES);
        EXPECT_SUCCESS(s2n_drbg_wipe(&kat_drbg));
    }

    fclose(kat_file);

    END_TEST();
}
