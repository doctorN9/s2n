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

#include <openssl/evp.h>

#include "error/s2n_errno.h"

#include "crypto/s2n_cipher.h"

#include "utils/s2n_safety.h"
#include "utils/s2n_blob.h"

static uint8_t s2n_cbc_cipher_3des_available()
{
    return (EVP_des_ede3_cbc() ? 1 : 0);
}

static int s2n_cbc_cipher_3des_encrypt(struct s2n_session_key *key, struct s2n_blob *iv, struct s2n_blob *in, struct s2n_blob *out)
{
    gte_check(out->size, in->size);

    S2N_ERROR_IF(EVP_EncryptInit_ex(key->evp_cipher_ctx, NULL, NULL, NULL, iv->data) == 0, S2N_ERR_KEY_INIT);

    int len = out->size;
    S2N_ERROR_IF(EVP_EncryptUpdate(key->evp_cipher_ctx, out->data, &len, in->data, in->size) == 0, S2N_ERR_ENCRYPT);

    S2N_ERROR_IF(len != in->size, S2N_ERR_ENCRYPT);

    return 0;
}

static int s2n_cbc_cipher_3des_decrypt(struct s2n_session_key *key, struct s2n_blob *iv, struct s2n_blob *in, struct s2n_blob *out)
{
    gte_check(out->size, in->size);

    S2N_ERROR_IF(EVP_DecryptInit_ex(key->evp_cipher_ctx, NULL, NULL, NULL, iv->data) == 0, S2N_ERR_KEY_INIT);

    int len = out->size;
    S2N_ERROR_IF(EVP_DecryptUpdate(key->evp_cipher_ctx, out->data, &len, in->data, in->size) == 0, S2N_ERR_DECRYPT);

    return 0;
}

static int s2n_cbc_cipher_3des_set_decryption_key(struct s2n_session_key *key, struct s2n_blob *in)
{
    eq_check(in->size, 192 / 8);

    EVP_CIPHER_CTX_set_padding(key->evp_cipher_ctx, EVP_CIPH_NO_PADDING);
    S2N_ERROR_IF(EVP_DecryptInit_ex(key->evp_cipher_ctx, EVP_des_ede3_cbc(), NULL, in->data, NULL) != 1, S2N_ERR_KEY_INIT);

    return 0;
}

static int s2n_cbc_cipher_3des_set_encryption_key(struct s2n_session_key *key, struct s2n_blob *in)
{
    eq_check(in->size, 192 / 8);

    EVP_CIPHER_CTX_set_padding(key->evp_cipher_ctx, EVP_CIPH_NO_PADDING);
    S2N_ERROR_IF(EVP_EncryptInit_ex(key->evp_cipher_ctx, EVP_des_ede3_cbc(), NULL, in->data, NULL) != 1, S2N_ERR_KEY_INIT);

    return 0;
}

static int s2n_cbc_cipher_3des_init(struct s2n_session_key *key)
{
    EVP_CIPHER_CTX_init(key->evp_cipher_ctx);

    return 0;
}

static int s2n_cbc_cipher_3des_destroy_key(struct s2n_session_key *key)
{
    EVP_CIPHER_CTX_cleanup(key->evp_cipher_ctx);

    return 0;
}

struct s2n_cipher s2n_3des = {
    .key_material_size = 24,
    .type = S2N_CBC,
    .io.cbc = {
               .block_size = 8,
               .record_iv_size = 8,
               .decrypt = s2n_cbc_cipher_3des_decrypt,
               .encrypt = s2n_cbc_cipher_3des_encrypt},
    .is_available = s2n_cbc_cipher_3des_available,
    .init = s2n_cbc_cipher_3des_init,
    .set_decryption_key = s2n_cbc_cipher_3des_set_decryption_key,
    .set_encryption_key = s2n_cbc_cipher_3des_set_encryption_key,
    .destroy_key = s2n_cbc_cipher_3des_destroy_key,
};
