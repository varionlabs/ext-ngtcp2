#ifndef PHP_NGTCP2_TYPES_H
#define PHP_NGTCP2_TYPES_H

#include "php.h"
#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <sys/socket.h>
#include <stdint.h>

typedef enum _php_quic_event_type {
  PHP_QUIC_EVENT_HANDSHAKE_COMPLETED = 1,
  PHP_QUIC_EVENT_CONNECTION_CLOSED = 2,
  PHP_QUIC_EVENT_CONNECTION_DRAINING = 3,
  PHP_QUIC_EVENT_STREAM_OPENED = 10,
  PHP_QUIC_EVENT_STREAM_READABLE = 11,
  PHP_QUIC_EVENT_STREAM_WRITABLE = 12,
  PHP_QUIC_EVENT_STREAM_CLOSED = 13,
  PHP_QUIC_EVENT_STREAM_RESET = 14
} php_quic_event_type;

typedef struct _php_quic_stream_entry {
  int64_t stream_id;
  zend_string *rx_buffer;
  zend_string *tx_buffer;
  uint64_t tx_offset;
  zend_bool readable;
  zend_bool writable;
  zend_bool closed;
  zend_bool reset;
  zend_bool fin_sent;
  zend_bool fin_recv;
  zend_bool fin_pending;
  zend_bool pending_open;
} php_quic_stream_entry;

typedef struct _php_quic_event {
  php_quic_event_type type;
  int64_t stream_id;
  uint64_t error_code;
  zend_bool by_peer;
  zend_string *reason;
  uint64_t timestamp;
} php_quic_event;

typedef struct _php_quic_event_node {
  php_quic_event event;
  struct _php_quic_event_node *next;
} php_quic_event_node;

typedef struct _php_quic_event_queue {
  php_quic_event_node *head;
  php_quic_event_node *tail;
  uint32_t size;
} php_quic_event_queue;

typedef struct _php_quic_connection {
  ngtcp2_conn *conn;
  ngtcp2_crypto_conn_ref conn_ref;
  gnutls_certificate_credentials_t cred;
  gnutls_session_t session;
  ngtcp2_ccerr last_error;
  struct sockaddr_storage remote_addr;
  struct sockaddr_storage local_addr;
  socklen_t remote_addrlen;
  socklen_t local_addrlen;
  zval remote_address_zv;
  zval local_address_zv;
  zend_string *close_reason;
  HashTable streams;
  php_quic_event_queue events;
  int64_t next_bidi_stream_id;
  int64_t next_uni_stream_id;
  zend_bool established;
  zend_bool draining;
  zend_bool closed;
  zend_bool close_requested;
  zend_bool close_packet_sent;
  zend_bool close_event_emitted;
  zend_bool draining_event_emitted;
  zend_object std;
} php_quic_connection;

typedef struct _php_quic_client_config {
  zval local_address;
  zend_string *server_name;
  zend_string *alpn;
  zend_object std;
} php_quic_client_config;

typedef struct _php_quic_server_config {
  zval local_address;
  zend_string *cert_file;
  zend_string *key_file;
  zend_string *alpn;
  zend_object std;
} php_quic_server_config;

typedef struct _php_quic_stream {
  zval connection;
  int64_t stream_id;
  zend_object std;
} php_quic_stream;

typedef struct _php_quic_address {
  zend_string *host;
  zend_long port;
  zend_object std;
} php_quic_address;

typedef struct _php_quic_datagram {
  zend_string *payload;
  zval peer_address;
  zval local_address;
  zend_long ecn;
  zend_long received_at;
  zend_bool has_ecn;
  zend_bool has_received_at;
  zend_object std;
} php_quic_datagram;

typedef struct _php_quic_event_object {
  php_quic_event_type type;
  int64_t stream_id;
  uint64_t error_code;
  zend_bool by_peer;
  zend_string *reason;
  uint64_t timestamp;
  zend_object std;
} php_quic_event_object;

#endif /* PHP_NGTCP2_TYPES_H */
