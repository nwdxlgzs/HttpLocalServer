/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HLS_THREADING_ALT_H
#define HLS_THREADING_ALT_H
/* Opaque to C; constructed and destroyed by the C++ callbacks. */
typedef struct mbedtls_threading_mutex_t { void* handle; } mbedtls_threading_mutex_t;
#endif
