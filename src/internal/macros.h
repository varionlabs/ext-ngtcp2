#ifndef PHP_NGTCP2_MACROS_H
#define PHP_NGTCP2_MACROS_H

#include "types.h"

#define Z_QUIC_CONNECTION_P(zv)                                                 \
  ((php_quic_connection *)((char *)Z_OBJ_P((zv)) -                              \
                           XtOffsetOf(php_quic_connection, std)))

#define Z_QUIC_CLIENT_CONFIG_P(zv)                                              \
  ((php_quic_client_config *)((char *)Z_OBJ_P((zv)) -                           \
                              XtOffsetOf(php_quic_client_config, std)))

#define Z_QUIC_SERVER_CONFIG_P(zv)                                              \
  ((php_quic_server_config *)((char *)Z_OBJ_P((zv)) -                           \
                              XtOffsetOf(php_quic_server_config, std)))

#define Z_QUIC_STREAM_P(zv)                                                     \
  ((php_quic_stream *)((char *)Z_OBJ_P((zv)) -                                  \
                       XtOffsetOf(php_quic_stream, std)))

#define Z_QUIC_ADDRESS_P(zv)                                                    \
  ((php_quic_address *)((char *)Z_OBJ_P((zv)) -                                 \
                        XtOffsetOf(php_quic_address, std)))

#define Z_QUIC_DATAGRAM_P(zv)                                                   \
  ((php_quic_datagram *)((char *)Z_OBJ_P((zv)) -                                \
                         XtOffsetOf(php_quic_datagram, std)))

#define Z_QUIC_EVENT_OBJ_P(zv)                                                  \
  ((php_quic_event_object *)((char *)Z_OBJ_P((zv)) -                            \
                             XtOffsetOf(php_quic_event_object, std)))

#endif /* PHP_NGTCP2_MACROS_H */
