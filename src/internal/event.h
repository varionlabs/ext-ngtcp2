#ifndef PHP_NGTCP2_EVENT_H
#define PHP_NGTCP2_EVENT_H

#include "php.h"
#include "types.h"

extern zend_class_entry *php_quic_event_ce;
extern zend_class_entry *php_quic_connection_event_ce;
extern zend_class_entry *php_quic_stream_event_ce;
extern zend_class_entry *php_quic_terminal_stream_event_ce;
extern zend_class_entry *php_quic_handshake_completed_ce;
extern zend_class_entry *php_quic_connection_closed_ce;
extern zend_class_entry *php_quic_connection_draining_ce;
extern zend_class_entry *php_quic_stream_opened_ce;
extern zend_class_entry *php_quic_stream_readable_ce;
extern zend_class_entry *php_quic_stream_writable_ce;
extern zend_class_entry *php_quic_stream_closed_ce;
extern zend_class_entry *php_quic_stream_reset_ce;

int php_ngtcp2_event_init(INIT_FUNC_ARGS);
void php_quic_event_create_from_native(zval *return_value,
                                       const php_quic_event *event);
zend_class_entry *php_quic_event_class_for_type(php_quic_event_type type);

#endif /* PHP_NGTCP2_EVENT_H */
