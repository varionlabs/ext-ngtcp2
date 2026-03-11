#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_hrtime.h>

#include "internal/address.h"
#include "internal/connection.h"
#include "internal/datagram.h"
#include "internal/event.h"
#include "internal/macros.h"
#include "internal/queue.h"
#include "internal/stream.h"
#include "internal/tls.h"

zend_class_entry *php_quic_connection_ce;
static zend_object_handlers php_quic_connection_handlers;

static void php_quic_stream_entry_ptr_dtor(zval *zv) {
  php_quic_stream_entry *entry = Z_PTR_P(zv);

  if (entry == NULL) {
    return;
  }

  if (entry->rx_buffer != NULL) {
    zend_string_release(entry->rx_buffer);
  }
  if (entry->tx_buffer != NULL) {
    zend_string_release(entry->tx_buffer);
  }

  efree(entry);
}

static php_quic_stream_entry *php_quic_stream_entry_alloc(int64_t stream_id) {
  php_quic_stream_entry *entry;

  entry = ecalloc(1, sizeof(*entry));
  entry->stream_id = stream_id;
  entry->rx_buffer = zend_string_init("", 0, 0);
  entry->tx_buffer = zend_string_init("", 0, 0);
  entry->readable = 0;
  entry->writable = 1;
  entry->closed = 0;
  entry->reset = 0;
  entry->fin_sent = 0;
  entry->fin_recv = 0;

  return entry;
}

void php_quic_connection_push_event(php_quic_connection *connection,
                                    php_quic_event_type type, int64_t stream_id,
                                    uint64_t error_code, zend_bool by_peer,
                                    const char *reason) {
  php_quic_event event;

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.stream_id = stream_id;
  event.error_code = error_code;
  event.by_peer = by_peer;
  event.reason = reason != NULL ? zend_string_init(reason, strlen(reason), 0) : NULL;
  event.timestamp = (uint64_t)(zend_hrtime() / 1000000);

  php_quic_event_queue_push(&connection->events, &event);
  php_quic_event_release(&event);
}

php_quic_stream_entry *php_quic_connection_get_stream_entry(
  php_quic_connection *connection, int64_t stream_id) {
  zval *zv;

  zv = zend_hash_index_find(&connection->streams, (zend_ulong)stream_id);
  if (zv == NULL) {
    return NULL;
  }

  return (php_quic_stream_entry *)Z_PTR_P(zv);
}

php_quic_stream_entry *php_quic_connection_open_stream_entry(
  php_quic_connection *connection, int64_t stream_id) {
  php_quic_stream_entry *entry;
  zval value;

  entry = php_quic_connection_get_stream_entry(connection, stream_id);
  if (entry != NULL) {
    return entry;
  }

  entry = php_quic_stream_entry_alloc(stream_id);
  ZVAL_PTR(&value, entry);
  zend_hash_index_update(&connection->streams, (zend_ulong)stream_id, &value);
  return entry;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_construct, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, remoteAddress, Ngtcp2\\Address, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Ngtcp2\\Address, 1, "null")
  ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_recv, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, datagram, Ngtcp2\\Datagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_no_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_get_next_timeout, 0, 0,
                                        IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_poll_events, 0, 0,
                                        IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_flush, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_connection_open_stream, 0, 0, Ngtcp2\\Stream,
                                       0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_close, 0, 0, 0)
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, errorCode, IS_LONG, 0, "0")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, reason, IS_STRING, 0, "\"\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_is_established, 0, 0, _IS_BOOL,
                                        0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_is_closed, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_is_draining, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_Connection, __construct) {
  php_quic_connection *connection;
  zval *remote_address;
  zval *local_address = NULL;
  zval *options = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 3)
    Z_PARAM_OBJECT_OF_CLASS(remote_address, php_quic_address_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_ARRAY_OR_NULL(options)
  ZEND_PARSE_PARAMETERS_END();

  (void)remote_address;
  (void)local_address;
  (void)options;

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->session == NULL && php_quic_tls_gnutls_init_client(connection) != SUCCESS) {
    zend_throw_exception(zend_ce_exception,
                         "Failed to initialize GnuTLS for QUIC connection", 0);
    RETURN_THROWS();
  }
}

PHP_METHOD(Ngtcp2_Connection, recv) {
  php_quic_connection *connection;
  zval *datagram;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_OBJECT_OF_CLASS(datagram, php_quic_datagram_ce)
  ZEND_PARSE_PARAMETERS_END();

  (void)datagram;

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (!connection->established) {
    connection->established = 1;
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_HANDSHAKE_COMPLETED, -1, 0, 0,
                                   NULL);
  }
}

PHP_METHOD(Ngtcp2_Connection, onTimeout) {
  ZEND_PARSE_PARAMETERS_NONE();
}

PHP_METHOD(Ngtcp2_Connection, getNextTimeout) {
  php_quic_connection *connection;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->closed) {
    RETURN_NULL();
  }

  RETURN_LONG(100);
}

PHP_METHOD(Ngtcp2_Connection, pollEvents) {
  php_quic_connection *connection;
  php_quic_event event;
  zval event_zv;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  array_init(return_value);

  while (php_quic_event_queue_pop(&connection->events, &event)) {
    php_quic_event_create_from_native(&event_zv, &event);
    add_next_index_zval(return_value, &event_zv);
    php_quic_event_release(&event);
  }
}

PHP_METHOD(Ngtcp2_Connection, flush) {
  ZEND_PARSE_PARAMETERS_NONE();
  array_init(return_value);
}

PHP_METHOD(Ngtcp2_Connection, openStream) {
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  int64_t stream_id;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->closed) {
    zend_throw_exception(zend_ce_exception, "Connection is closed", 0);
    RETURN_THROWS();
  }

  stream_id = connection->next_stream_id;
  connection->next_stream_id += 4;

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->writable = 1;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_OPENED, stream_id, 0, 0,
                                 NULL);
  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE, stream_id, 0, 0,
                                 NULL);

  php_quic_stream_create(return_value, ZEND_THIS, stream_id);
}

PHP_METHOD(Ngtcp2_Connection, close) {
  php_quic_connection *connection;
  zend_long error_code = 0;
  zend_string *reason = NULL;

  ZEND_PARSE_PARAMETERS_START(0, 2)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(error_code)
    Z_PARAM_STR(reason)
  ZEND_PARSE_PARAMETERS_END();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->closed) {
    return;
  }

  connection->draining = 0;
  connection->closed = 1;
  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_CONNECTION_CLOSED, -1,
                                 (uint64_t)error_code, 0,
                                 reason != NULL ? ZSTR_VAL(reason) : NULL);
}

PHP_METHOD(Ngtcp2_Connection, isEstablished) {
  php_quic_connection *connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_BOOL(connection->established);
}

PHP_METHOD(Ngtcp2_Connection, isClosed) {
  php_quic_connection *connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_BOOL(connection->closed);
}

PHP_METHOD(Ngtcp2_Connection, isDraining) {
  php_quic_connection *connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_BOOL(connection->draining);
}

static const zend_function_entry php_quic_connection_methods[] = {
  PHP_ME(Ngtcp2_Connection, __construct, arginfo_connection_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, recv, arginfo_connection_recv, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, onTimeout, arginfo_connection_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, getNextTimeout, arginfo_connection_get_next_timeout,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, pollEvents, arginfo_connection_poll_events, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, flush, arginfo_connection_flush, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, openStream, arginfo_connection_open_stream, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, close, arginfo_connection_close, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, isEstablished, arginfo_connection_is_established, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, isClosed, arginfo_connection_is_closed, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, isDraining, arginfo_connection_is_draining, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_connection_create_object(zend_class_entry *ce) {
  php_quic_connection *connection;

  connection = zend_object_alloc(sizeof(*connection), ce);
  connection->conn = NULL;
  connection->cred = NULL;
  connection->session = NULL;
  connection->next_stream_id = 0;
  connection->established = 0;
  connection->draining = 0;
  connection->closed = 0;

  zend_hash_init(&connection->streams, 0, NULL, php_quic_stream_entry_ptr_dtor, 0);
  php_quic_event_queue_init(&connection->events);

  zend_object_std_init(&connection->std, ce);
  object_properties_init(&connection->std, ce);
  connection->std.handlers = &php_quic_connection_handlers;

  return &connection->std;
}

static void php_quic_connection_free_object(zend_object *object) {
  php_quic_connection *connection;

  connection = (php_quic_connection *)((char *)object -
                                       XtOffsetOf(php_quic_connection, std));

  if (connection->conn != NULL) {
    ngtcp2_conn_del(connection->conn);
    connection->conn = NULL;
  }

  php_quic_tls_gnutls_cleanup(connection);
  php_quic_event_queue_destroy(&connection->events);
  zend_hash_destroy(&connection->streams);
  zend_object_std_dtor(&connection->std);
}

int php_ngtcp2_connection_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Ngtcp2", "Connection", php_quic_connection_methods);
  php_quic_connection_ce = zend_register_internal_class(&ce);
  php_quic_connection_ce->create_object = php_quic_connection_create_object;

  memcpy(&php_quic_connection_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_connection_handlers.offset = XtOffsetOf(php_quic_connection, std);
  php_quic_connection_handlers.free_obj = php_quic_connection_free_object;

  return SUCCESS;
}
