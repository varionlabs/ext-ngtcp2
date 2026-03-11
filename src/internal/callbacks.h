#ifndef PHP_NGTCP2_CALLBACKS_H
#define PHP_NGTCP2_CALLBACKS_H

#include <ngtcp2/ngtcp2.h>

void php_quic_callbacks_init(ngtcp2_callbacks *callbacks);

#endif /* PHP_NGTCP2_CALLBACKS_H */
