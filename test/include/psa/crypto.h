/**
 * @file crypto.h
 * @brief Minimal PSA Crypto mock for host tests (ESP-IDF 6.x MbedTLS 3.x).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "mbedtls/sha256.h"

typedef int32_t psa_status_t;
typedef uint32_t psa_key_id_t;

#define PSA_SUCCESS                  0
#define PSA_ERROR_GENERIC_ERROR     -1
#define PSA_ERROR_INVALID_SIGNATURE -2
#define PSA_ERROR_INSUFFICIENT_MEMORY -3
#define PSA_ERROR_INVALID_ARGUMENT  -4

#define PSA_KEY_USAGE_EXPORT   0x00000001
#define PSA_KEY_USAGE_ENCRYPT  0x00000002
#define PSA_KEY_USAGE_DECRYPT  0x00000004
#define PSA_KEY_USAGE_DERIVE   0x00000008

#define PSA_ALG_ECDH           0x00000010
#define PSA_ECC_FAMILY_MONTGOMERY 0x00000020
#define PSA_KEY_TYPE_ECC_KEY_PAIR(family) (0x00000100 | (family))
#define PSA_KEY_TYPE_AES       0x00000200
#define PSA_ALG_CCM            0x00000400
#define PSA_ALG_SHA_256        0x00000800
#define PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, tag_len) ((alg) | (((uint32_t)(tag_len)) << 16))

typedef struct {
    uint32_t usage;
    uint32_t alg;
    uint32_t type;
    size_t   bits;
} psa_key_attributes_t;

#define PSA_KEY_ATTRIBUTES_INIT {0, 0, 0, 0}

static inline void psa_set_key_usage_flags(psa_key_attributes_t *attributes, uint32_t usage_flags) {
    if (attributes) attributes->usage = usage_flags;
}

static inline void psa_set_key_algorithm(psa_key_attributes_t *attributes, uint32_t alg) {
    if (attributes) attributes->alg = alg;
}

static inline void psa_set_key_type(psa_key_attributes_t *attributes, uint32_t type) {
    if (attributes) attributes->type = type;
}

static inline void psa_set_key_bits(psa_key_attributes_t *attributes, size_t bits) {
    if (attributes) attributes->bits = bits;
}

typedef struct {
    bool in_use;
    psa_key_attributes_t attr;
    uint8_t key_data[64];
    size_t key_len;
} _psa_mock_slot_t;

static _psa_mock_slot_t _psa_slots[32];
static uint32_t _psa_key_counter = 1;

static inline psa_status_t psa_crypto_init(void) {
    return PSA_SUCCESS;
}

static inline psa_status_t psa_generate_key(const psa_key_attributes_t *attributes, psa_key_id_t *key_id) {
    if (!attributes || !key_id) return PSA_ERROR_INVALID_ARGUMENT;
    for (size_t i = 1; i < 32; i++) {
        if (!_psa_slots[i].in_use) {
            _psa_slots[i].in_use = true;
            _psa_slots[i].attr = *attributes;
            _psa_slots[i].key_len = 32;
            uint32_t id = _psa_key_counter++;
            for (size_t j = 0; j < 32; j++) {
                _psa_slots[i].key_data[j] = (uint8_t)((id * 31 + j + 7) & 0xFF);
                if (_psa_slots[i].key_data[j] == 0) _psa_slots[i].key_data[j] = 0xAA;
            }
            *key_id = (psa_key_id_t)i;
            return PSA_SUCCESS;
        }
    }
    return PSA_ERROR_INSUFFICIENT_MEMORY;
}

static inline psa_status_t psa_export_public_key(psa_key_id_t key_id, uint8_t *data, size_t data_size, size_t *data_length) {
    if (key_id >= 32 || !_psa_slots[key_id].in_use) return PSA_ERROR_INVALID_ARGUMENT;
    if (data_size < _psa_slots[key_id].key_len) return PSA_ERROR_INVALID_ARGUMENT;
    memcpy(data, _psa_slots[key_id].key_data, _psa_slots[key_id].key_len);
    if (data_length) *data_length = _psa_slots[key_id].key_len;
    return PSA_SUCCESS;
}

static inline psa_status_t psa_raw_key_agreement(uint32_t alg, psa_key_id_t key_id,
                                                const uint8_t *peer_key, size_t peer_key_size,
                                                uint8_t *output, size_t output_size, size_t *output_len) {
    (void)alg;
    if (key_id >= 32 || !_psa_slots[key_id].in_use) return PSA_ERROR_INVALID_ARGUMENT;
    if (!peer_key || output_size < 32) return PSA_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < 32 && i < peer_key_size && i < _psa_slots[key_id].key_len; i++) {
        output[i] = (uint8_t)(_psa_slots[key_id].key_data[i] ^ peer_key[i]);
    }
    if (output_len) *output_len = 32;
    return PSA_SUCCESS;
}

static inline psa_status_t psa_hash_compute(uint32_t alg, const uint8_t *input, size_t input_length,
                                            uint8_t *hash, size_t hash_size, size_t *hash_length) {
    (void)alg;
    if (hash_size < 32) return PSA_ERROR_INVALID_ARGUMENT;
    if (mbedtls_sha256(input, input_length, hash, 0) != 0) return PSA_ERROR_GENERIC_ERROR;
    if (hash_length) *hash_length = 32;
    return PSA_SUCCESS;
}

static inline psa_status_t psa_import_key(const psa_key_attributes_t *attributes,
                                         const uint8_t *data, size_t data_length,
                                         psa_key_id_t *key_id) {
    if (!attributes || !data || !key_id) return PSA_ERROR_INVALID_ARGUMENT;
    for (size_t i = 1; i < 32; i++) {
        if (!_psa_slots[i].in_use) {
            _psa_slots[i].in_use = true;
            _psa_slots[i].attr = *attributes;
            _psa_slots[i].key_len = data_length < 64 ? data_length : 64;
            memcpy(_psa_slots[i].key_data, data, _psa_slots[i].key_len);
            *key_id = (psa_key_id_t)i;
            return PSA_SUCCESS;
        }
    }
    return PSA_ERROR_INSUFFICIENT_MEMORY;
}

static inline psa_status_t psa_destroy_key(psa_key_id_t key_id) {
    if (key_id < 32) {
        memset(&_psa_slots[key_id], 0, sizeof(_psa_slots[key_id]));
    }
    return PSA_SUCCESS;
}

static inline void _psa_compute_mac(const uint8_t *k, size_t klen,
                                    const uint8_t *nonce, size_t nonce_len,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t *pt, size_t pt_len,
                                    uint8_t *tag, size_t tag_len) {
    uint8_t buf[512];
    size_t off = 0;
    if (k && klen && off + klen <= sizeof(buf)) { memcpy(buf + off, k, klen); off += klen; }
    if (nonce && nonce_len && off + nonce_len <= sizeof(buf)) { memcpy(buf + off, nonce, nonce_len); off += nonce_len; }
    if (aad && aad_len && off + aad_len <= sizeof(buf)) { memcpy(buf + off, aad, aad_len); off += aad_len; }
    if (pt && pt_len && off + pt_len <= sizeof(buf)) { memcpy(buf + off, pt, pt_len); off += pt_len; }
    uint8_t hash[32];
    mbedtls_sha256(buf, off, hash, 0);
    for (size_t i = 0; i < tag_len && i < 32; i++) {
        tag[i] = hash[i];
    }
}

static inline psa_status_t psa_aead_encrypt(psa_key_id_t key_id, uint32_t alg,
                                            const uint8_t *nonce, size_t nonce_len,
                                            const uint8_t *additional_data, size_t additional_data_len,
                                            const uint8_t *plaintext, size_t plaintext_len,
                                            uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_len) {
    if (key_id >= 32 || !_psa_slots[key_id].in_use) return PSA_ERROR_INVALID_ARGUMENT;
    size_t tag_len = (alg >> 16) & 0xFF;
    if (tag_len == 0) tag_len = 4;
    if (ciphertext_size < plaintext_len + tag_len) return PSA_ERROR_INVALID_ARGUMENT;

    const uint8_t *k = _psa_slots[key_id].key_data;
    size_t klen = _psa_slots[key_id].key_len > 0 ? _psa_slots[key_id].key_len : 16;

    for (size_t i = 0; i < plaintext_len; i++) {
        uint8_t n_byte = (nonce && nonce_len > 0) ? nonce[i % nonce_len] : 0;
        ciphertext[i] = (uint8_t)(plaintext[i] ^ k[i % klen] ^ n_byte);
    }
    _psa_compute_mac(k, klen, nonce, nonce_len, additional_data, additional_data_len,
                     plaintext, plaintext_len, ciphertext + plaintext_len, tag_len);
    if (ciphertext_len) *ciphertext_len = plaintext_len + tag_len;
    return PSA_SUCCESS;
}

static inline psa_status_t psa_aead_decrypt(psa_key_id_t key_id, uint32_t alg,
                                            const uint8_t *nonce, size_t nonce_len,
                                            const uint8_t *additional_data, size_t additional_data_len,
                                            const uint8_t *ciphertext, size_t ciphertext_len,
                                            uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_len) {
    if (key_id >= 32 || !_psa_slots[key_id].in_use) return PSA_ERROR_INVALID_ARGUMENT;
    size_t tag_len = (alg >> 16) & 0xFF;
    if (tag_len == 0) tag_len = 4;
    if (ciphertext_len < tag_len) return PSA_ERROR_INVALID_ARGUMENT;
    size_t body_len = ciphertext_len - tag_len;
    if (plaintext_size < body_len) return PSA_ERROR_INVALID_ARGUMENT;

    const uint8_t *k = _psa_slots[key_id].key_data;
    size_t klen = _psa_slots[key_id].key_len > 0 ? _psa_slots[key_id].key_len : 16;

    uint8_t temp_pt[256];
    uint8_t *target = plaintext ? plaintext : temp_pt;

    for (size_t i = 0; i < body_len; i++) {
        uint8_t n_byte = (nonce && nonce_len > 0) ? nonce[i % nonce_len] : 0;
        target[i] = (uint8_t)(ciphertext[i] ^ k[i % klen] ^ n_byte);
    }
    uint8_t expected_tag[16];
    _psa_compute_mac(k, klen, nonce, nonce_len, additional_data, additional_data_len,
                     target, body_len, expected_tag, tag_len);
    if (memcmp(ciphertext + body_len, expected_tag, tag_len) != 0) {
        return PSA_ERROR_INVALID_SIGNATURE;
    }
    if (plaintext_len) *plaintext_len = body_len;
    return PSA_SUCCESS;
}
