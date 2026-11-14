/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* std::mutex adapters also protect the PSA globals and shared RSA blinding. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
/* No network implementation or persistent PSA key store is used by this app. */
#undef MBEDTLS_NET_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C
