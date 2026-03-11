#ifndef PHP_NGTCP2_CONNECTION_H
#define PHP_NGTCP2_CONNECTION_H

#include "php.h"
#include "types.h"

extern zend_class_entry *php_quic_connection_ce;

int php_ngtcp2_connection_init(INIT_FUNC_ARGS);
php_quic_stream_entry *php_quic_connection_get_stream_entry(
  php_quic_connection *connection, int64_t stream_id);
php_quic_stream_entry *php_quic_connection_open_stream_entry(
  php_quic_connection *connection, int64_t stream_id);
void php_quic_connection_push_event(php_quic_connection *connection,
                                    php_quic_event_type type, int64_t stream_id,
                                    uint64_t error_code, zend_bool by_peer,
                                    const char *reason);

#endif /* PHP_NGTCP2_CONNECTION_H */
