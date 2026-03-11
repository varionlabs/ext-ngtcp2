#ifndef PHP_NGTCP2_TLS_H
#define PHP_NGTCP2_TLS_H

#include "types.h"

int php_quic_tls_gnutls_init_client(php_quic_connection *connection);
void php_quic_tls_gnutls_cleanup(php_quic_connection *connection);

#endif /* PHP_NGTCP2_TLS_H */
