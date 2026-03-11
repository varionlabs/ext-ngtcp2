#ifndef PHP_NGTCP2_DATAGRAM_H
#define PHP_NGTCP2_DATAGRAM_H

#include "php.h"

extern zend_class_entry *php_quic_datagram_ce;

int php_ngtcp2_datagram_init(INIT_FUNC_ARGS);

#endif /* PHP_NGTCP2_DATAGRAM_H */
