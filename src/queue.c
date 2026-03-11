#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "internal/queue.h"

void php_quic_event_release(php_quic_event *event) {
  if (event->reason != NULL) {
    zend_string_release(event->reason);
    event->reason = NULL;
  }
}

void php_quic_event_queue_init(php_quic_event_queue *queue) {
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
}

void php_quic_event_queue_destroy(php_quic_event_queue *queue) {
  php_quic_event event;

  while (php_quic_event_queue_pop(queue, &event)) {
    php_quic_event_release(&event);
  }
}

void php_quic_event_queue_push(php_quic_event_queue *queue,
                               const php_quic_event *event) {
  php_quic_event_node *node;

  node = emalloc(sizeof(*node));
  memset(node, 0, sizeof(*node));
  node->event = *event;

  if (node->event.reason != NULL) {
    zend_string_addref(node->event.reason);
  }

  if (queue->tail != NULL) {
    queue->tail->next = node;
  } else {
    queue->head = node;
  }

  queue->tail = node;
  queue->size++;
}

zend_bool php_quic_event_queue_pop(php_quic_event_queue *queue,
                                   php_quic_event *event) {
  php_quic_event_node *node;

  if (queue->head == NULL) {
    return 0;
  }

  node = queue->head;
  queue->head = node->next;

  if (queue->head == NULL) {
    queue->tail = NULL;
  }

  *event = node->event;
  efree(node);
  queue->size--;
  return 1;
}
