#ifndef PHP_NGTCP2_QUEUE_H
#define PHP_NGTCP2_QUEUE_H

#include "types.h"

void php_quic_event_queue_init(php_quic_event_queue *queue);
void php_quic_event_queue_destroy(php_quic_event_queue *queue);
void php_quic_event_queue_push(php_quic_event_queue *queue,
                               const php_quic_event *event);
zend_bool php_quic_event_queue_pop(php_quic_event_queue *queue,
                                   php_quic_event *event);
void php_quic_event_release(php_quic_event *event);

#endif /* PHP_NGTCP2_QUEUE_H */
