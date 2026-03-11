#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "internal/callbacks.h"

void php_quic_callbacks_init(ngtcp2_callbacks *callbacks) {
  memset(callbacks, 0, sizeof(*callbacks));
}
