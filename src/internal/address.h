#ifndef PHP_NGTCP2_ADDRESS_H
#define PHP_NGTCP2_ADDRESS_H

#include "php.h"
#include <sys/socket.h>

extern zend_class_entry *php_quic_address_ce;

int php_ngtcp2_address_init(INIT_FUNC_ARGS);
int php_quic_address_to_sockaddr(const zval *address_zv,
                                 struct sockaddr_storage *storage,
                                 socklen_t *addrlen);
void php_quic_address_init_from_sockaddr(zval *return_value,
                                         const struct sockaddr *addr,
                                         socklen_t addrlen);

#endif /* PHP_NGTCP2_ADDRESS_H */
