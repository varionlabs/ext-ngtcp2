#ifndef PHP_NGTCP2_SERVER_ENDPOINT_H
#define PHP_NGTCP2_SERVER_ENDPOINT_H

#include "php.h"

extern zend_class_entry *php_quic_server_endpoint_ce;

int php_ngtcp2_server_endpoint_init(INIT_FUNC_ARGS);

#endif /* PHP_NGTCP2_SERVER_ENDPOINT_H */
