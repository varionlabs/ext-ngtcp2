#ifndef PHP_NGTCP2_ADDRESS_H
#define PHP_NGTCP2_ADDRESS_H

#include "php.h"

extern zend_class_entry *php_quic_address_ce;

int php_ngtcp2_address_init(INIT_FUNC_ARGS);

#endif /* PHP_NGTCP2_ADDRESS_H */
