#ifndef PHP_NGTCP2_BUFFER_H
#define PHP_NGTCP2_BUFFER_H

#include "php.h"

void php_quic_buffer_append(zend_string **buffer, const char *data, size_t len);
zend_string *php_quic_buffer_read(zend_string **buffer, size_t max_len);

#endif /* PHP_NGTCP2_BUFFER_H */
