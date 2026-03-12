#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <Zend/zend_exceptions.h>

#include "internal/buffer.h"
#include "internal/connection.h"
#include "internal/event.h"
#include "internal/macros.h"
#include "internal/stream.h"

zend_class_entry *php_quic_stream_ce;
static zend_object_handlers php_quic_stream_handlers;

static php_quic_connection *php_quic_stream_resolve_connection(
  php_quic_stream *stream) {
  if (Z_ISUNDEF(stream->connection) || Z_TYPE(stream->connection) != IS_OBJECT) {
    return NULL;
  }

  return Z_QUIC_CONNECTION_P(&stream->connection);
}

static php_quic_stream_entry *php_quic_stream_resolve_entry(php_quic_stream *stream) {
  php_quic_connection *connection;

  connection = php_quic_stream_resolve_connection(stream);
  if (connection == NULL) {
    return NULL;
  }

  return php_quic_connection_get_stream_entry(connection, stream->stream_id);
}

static zend_bool php_quic_stream_is_remote_unidirectional(
  php_quic_connection *connection, int64_t stream_id) {
  if (connection == NULL || connection->conn == NULL || ngtcp2_is_bidi_stream(stream_id)) {
    return 0;
  }

  return ngtcp2_conn_is_local_stream(connection->conn, stream_id) == 0;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_stream_construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_get_id, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_read, 0, 0, IS_STRING, 0)
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, length, IS_LONG, 0, "8192")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_write, 0, 1, IS_LONG, 0)
  ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stream_end, 0, 0, 0)
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, finalData, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stream_reset, 0, 0, 1)
  ZEND_ARG_TYPE_INFO(0, errorCode, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_is_readable, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_is_writable, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_is_closed, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_Stream, __construct) {
  zend_throw_exception(zend_ce_exception,
                       "Varion\\Ngtcp2\\Stream cannot be constructed directly", 0);
}

PHP_METHOD(Ngtcp2_Stream, getId) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  RETURN_LONG((zend_long)stream->stream_id);
}

PHP_METHOD(Ngtcp2_Stream, read) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_stream_entry *entry;
  zend_long length = 8192;
  zend_string *chunk;

  ZEND_PARSE_PARAMETERS_START(0, 1)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(length)
  ZEND_PARSE_PARAMETERS_END();

  if (length <= 0) {
    zend_argument_value_error(1, "must be greater than 0");
    RETURN_THROWS();
  }

  entry = php_quic_stream_resolve_entry(stream);
  if (entry == NULL || entry->rx_buffer == NULL || ZSTR_LEN(entry->rx_buffer) == 0) {
    RETURN_EMPTY_STRING();
  }

  chunk = php_quic_buffer_read(&entry->rx_buffer, (size_t)length);
  entry->readable = ZSTR_LEN(entry->rx_buffer) > 0;
  RETURN_STR(chunk);
}

PHP_METHOD(Ngtcp2_Stream, write) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  zend_string *data;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STR(data)
  ZEND_PARSE_PARAMETERS_END();

  entry = php_quic_stream_resolve_entry(stream);
  connection = php_quic_stream_resolve_connection(stream);
  if (entry == NULL) {
    zend_throw_exception(zend_ce_exception, "Stream entry does not exist", 0);
    RETURN_THROWS();
  }

  if (php_quic_stream_is_remote_unidirectional(connection, stream->stream_id)) {
    zend_throw_exception(zend_ce_exception,
                         "Cannot write to a remote unidirectional stream", 0);
    RETURN_THROWS();
  }

  if (entry->closed || entry->fin_sent || entry->fin_pending) {
    zend_throw_exception(zend_ce_exception, "Stream is not writable", 0);
    RETURN_THROWS();
  }

  php_quic_buffer_append(&entry->tx_buffer, ZSTR_VAL(data), ZSTR_LEN(data));
  entry->writable = 0;
  RETURN_LONG((zend_long)ZSTR_LEN(data));
}

PHP_METHOD(Ngtcp2_Stream, end) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  zend_string *final_data = NULL;

  ZEND_PARSE_PARAMETERS_START(0, 1)
    Z_PARAM_OPTIONAL
    Z_PARAM_STR_OR_NULL(final_data)
  ZEND_PARSE_PARAMETERS_END();

  entry = php_quic_stream_resolve_entry(stream);
  connection = php_quic_stream_resolve_connection(stream);
  if (entry == NULL) {
    zend_throw_exception(zend_ce_exception, "Stream entry does not exist", 0);
    RETURN_THROWS();
  }

  if (php_quic_stream_is_remote_unidirectional(connection, stream->stream_id)) {
    zend_throw_exception(zend_ce_exception,
                         "Cannot end a remote unidirectional stream", 0);
    RETURN_THROWS();
  }

  if (final_data != NULL) {
    php_quic_buffer_append(&entry->tx_buffer, ZSTR_VAL(final_data), ZSTR_LEN(final_data));
  }
  entry->fin_pending = 1;
  entry->writable = 0;
}

PHP_METHOD(Ngtcp2_Stream, reset) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_stream_entry *entry;
  php_quic_connection *connection;
  zend_long error_code;
  int rv = 0;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_LONG(error_code)
  ZEND_PARSE_PARAMETERS_END();

  entry = php_quic_stream_resolve_entry(stream);
  if (entry == NULL) {
    zend_throw_exception(zend_ce_exception, "Stream entry does not exist", 0);
    RETURN_THROWS();
  }

  if (Z_ISUNDEF(stream->connection) || Z_TYPE(stream->connection) != IS_OBJECT) {
    zend_throw_exception(zend_ce_exception, "Stream connection is not available", 0);
    RETURN_THROWS();
  }

  connection = Z_QUIC_CONNECTION_P(&stream->connection);
  if (connection->conn != NULL && !connection->closed) {
    rv = ngtcp2_conn_shutdown_stream(connection->conn, 0, stream->stream_id,
                                     (uint64_t)error_code);
    if (rv != 0) {
      zend_throw_exception_ex(zend_ce_exception, 0,
                              "ngtcp2_conn_shutdown_stream failed: %s",
                              ngtcp2_strerror(rv));
      RETURN_THROWS();
    }
  }

  entry->reset = 1;
  entry->closed = 1;
  entry->readable = 0;
  entry->writable = 0;
  entry->fin_pending = 0;
  entry->pending_open = 0;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_RESET,
                                 stream->stream_id, (uint64_t)error_code, 0, NULL);
}

PHP_METHOD(Ngtcp2_Stream, isReadable) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_stream_entry *entry;

  ZEND_PARSE_PARAMETERS_NONE();
  entry = php_quic_stream_resolve_entry(stream);
  RETURN_BOOL(entry != NULL && entry->readable);
}

PHP_METHOD(Ngtcp2_Stream, isWritable) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_stream_entry *entry;

  ZEND_PARSE_PARAMETERS_NONE();
  entry = php_quic_stream_resolve_entry(stream);
  RETURN_BOOL(entry != NULL && entry->writable && !entry->closed);
}

PHP_METHOD(Ngtcp2_Stream, isClosed) {
  php_quic_stream *stream = Z_QUIC_STREAM_P(ZEND_THIS);
  php_quic_stream_entry *entry;

  ZEND_PARSE_PARAMETERS_NONE();
  entry = php_quic_stream_resolve_entry(stream);
  RETURN_BOOL(entry == NULL || entry->closed);
}

static const zend_function_entry php_quic_stream_methods[] = {
  PHP_ME(Ngtcp2_Stream, __construct, arginfo_stream_construct,
         ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
  PHP_ME(Ngtcp2_Stream, getId, arginfo_stream_get_id, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, read, arginfo_stream_read, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, write, arginfo_stream_write, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, end, arginfo_stream_end, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, reset, arginfo_stream_reset, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, isReadable, arginfo_stream_is_readable, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, isWritable, arginfo_stream_is_writable, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Stream, isClosed, arginfo_stream_is_closed, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_stream_create_object(zend_class_entry *ce) {
  php_quic_stream *stream;

  stream = zend_object_alloc(sizeof(*stream), ce);
  ZVAL_UNDEF(&stream->connection);
  stream->stream_id = -1;

  zend_object_std_init(&stream->std, ce);
  object_properties_init(&stream->std, ce);
  stream->std.handlers = &php_quic_stream_handlers;

  return &stream->std;
}

static void php_quic_stream_free_object(zend_object *object) {
  php_quic_stream *stream;

  stream = (php_quic_stream *)((char *)object - XtOffsetOf(php_quic_stream, std));
  if (!Z_ISUNDEF(stream->connection)) {
    zval_ptr_dtor(&stream->connection);
    ZVAL_UNDEF(&stream->connection);
  }

  zend_object_std_dtor(&stream->std);
}

void php_quic_stream_create(zval *return_value, zval *connection_zv,
                            int64_t stream_id) {
  php_quic_stream *stream;

  object_init_ex(return_value, php_quic_stream_ce);
  stream = Z_QUIC_STREAM_P(return_value);
  ZVAL_COPY(&stream->connection, connection_zv);
  stream->stream_id = stream_id;
}

int php_ngtcp2_stream_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Stream", php_quic_stream_methods);
  php_quic_stream_ce = zend_register_internal_class(&ce);
  php_quic_stream_ce->create_object = php_quic_stream_create_object;

  memcpy(&php_quic_stream_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_stream_handlers.offset = XtOffsetOf(php_quic_stream, std);
  php_quic_stream_handlers.free_obj = php_quic_stream_free_object;

  return SUCCESS;
}
