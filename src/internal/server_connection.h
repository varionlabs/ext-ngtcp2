#ifndef PHP_NGTCP2_SERVER_CONNECTION_H
#define PHP_NGTCP2_SERVER_CONNECTION_H

#include "php.h"

extern zend_class_entry *php_quic_server_connection_ce;

int php_ngtcp2_server_connection_init(INIT_FUNC_ARGS);

#endif /* PHP_NGTCP2_SERVER_CONNECTION_H */
