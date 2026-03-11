#ifndef PHP_NGTCP2_STREAM_H
#define PHP_NGTCP2_STREAM_H

#include "php.h"
#include <stdint.h>

extern zend_class_entry *php_quic_stream_ce;

int php_ngtcp2_stream_init(INIT_FUNC_ARGS);
void php_quic_stream_create(zval *return_value, zval *connection_zv,
                            int64_t stream_id);

#endif /* PHP_NGTCP2_STREAM_H */
