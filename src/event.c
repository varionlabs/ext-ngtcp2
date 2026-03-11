#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <Zend/zend_hrtime.h>

#include "internal/event.h"
#include "internal/macros.h"

zend_class_entry *php_quic_event_ce;
zend_class_entry *php_quic_connection_event_ce;
zend_class_entry *php_quic_stream_event_ce;
zend_class_entry *php_quic_terminal_stream_event_ce;
zend_class_entry *php_quic_handshake_completed_ce;
zend_class_entry *php_quic_connection_closed_ce;
zend_class_entry *php_quic_connection_draining_ce;
zend_class_entry *php_quic_stream_opened_ce;
zend_class_entry *php_quic_stream_readable_ce;
zend_class_entry *php_quic_stream_writable_ce;
zend_class_entry *php_quic_stream_closed_ce;
zend_class_entry *php_quic_stream_reset_ce;

static zend_object_handlers php_quic_event_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_event_construct, 0, 0, 1)
  ZEND_ARG_TYPE_INFO(0, type, IS_LONG, 0)
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, streamId, IS_LONG, 0, "-1")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, errorCode, IS_LONG, 0, "0")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, byPeer, _IS_BOOL, 0, "false")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, reason, IS_STRING, 0, "\"\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_get_type, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_get_stream_id, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_get_error_code, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_is_by_peer, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_get_reason, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_event_get_timestamp, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_Event, __construct) {
  php_quic_event_object *event_obj;
  zend_long type;
  zend_long stream_id = -1;
  zend_long error_code = 0;
  zend_bool by_peer = 0;
  zend_string *reason = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 5)
    Z_PARAM_LONG(type)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(stream_id)
    Z_PARAM_LONG(error_code)
    Z_PARAM_BOOL(by_peer)
    Z_PARAM_STR(reason)
  ZEND_PARSE_PARAMETERS_END();

  event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  event_obj->type = (php_quic_event_type)type;
  event_obj->stream_id = stream_id;
  event_obj->error_code = (uint64_t)error_code;
  event_obj->by_peer = by_peer;
  event_obj->timestamp = (uint64_t)(zend_hrtime() / 1000000);

  if (event_obj->reason != NULL) {
    zend_string_release(event_obj->reason);
  }
  if (reason != NULL) {
    event_obj->reason = zend_string_copy(reason);
  } else {
    event_obj->reason = zend_string_init("", 0, 0);
  }
}

PHP_METHOD(Ngtcp2_Event, getType) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_LONG((zend_long)event_obj->type);
}

PHP_METHOD(Ngtcp2_Event, getStreamId) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_LONG((zend_long)event_obj->stream_id);
}

PHP_METHOD(Ngtcp2_Event, getErrorCode) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_LONG((zend_long)event_obj->error_code);
}

PHP_METHOD(Ngtcp2_Event, isByPeer) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_BOOL(event_obj->by_peer);
}

PHP_METHOD(Ngtcp2_Event, getReason) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_STR_COPY(event_obj->reason);
}

PHP_METHOD(Ngtcp2_Event, getTimestamp) {
  php_quic_event_object *event_obj = Z_QUIC_EVENT_OBJ_P(ZEND_THIS);
  RETURN_LONG((zend_long)event_obj->timestamp);
}

static const zend_function_entry php_quic_event_methods[] = {
  PHP_ME(Ngtcp2_Event, __construct, arginfo_event_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, getType, arginfo_event_get_type, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, getStreamId, arginfo_event_get_stream_id, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, getErrorCode, arginfo_event_get_error_code, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, isByPeer, arginfo_event_is_by_peer, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, getReason, arginfo_event_get_reason, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Event, getTimestamp, arginfo_event_get_timestamp, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_event_create_object(zend_class_entry *ce) {
  php_quic_event_object *event_obj;

  event_obj = zend_object_alloc(sizeof(*event_obj), ce);
  event_obj->type = PHP_QUIC_EVENT_STREAM_OPENED;
  event_obj->stream_id = -1;
  event_obj->error_code = 0;
  event_obj->by_peer = 0;
  event_obj->reason = zend_string_init("", 0, 0);
  event_obj->timestamp = 0;

  zend_object_std_init(&event_obj->std, ce);
  object_properties_init(&event_obj->std, ce);
  event_obj->std.handlers = &php_quic_event_handlers;

  return &event_obj->std;
}

static void php_quic_event_free_object(zend_object *object) {
  php_quic_event_object *event_obj;

  event_obj = (php_quic_event_object *)((char *)object -
                                        XtOffsetOf(php_quic_event_object, std));
  if (event_obj->reason != NULL) {
    zend_string_release(event_obj->reason);
    event_obj->reason = NULL;
  }

  zend_object_std_dtor(&event_obj->std);
}

zend_class_entry *php_quic_event_class_for_type(php_quic_event_type type) {
  switch (type) {
  case PHP_QUIC_EVENT_HANDSHAKE_COMPLETED:
    return php_quic_handshake_completed_ce;
  case PHP_QUIC_EVENT_CONNECTION_CLOSED:
    return php_quic_connection_closed_ce;
  case PHP_QUIC_EVENT_CONNECTION_DRAINING:
    return php_quic_connection_draining_ce;
  case PHP_QUIC_EVENT_STREAM_READABLE:
    return php_quic_stream_readable_ce;
  case PHP_QUIC_EVENT_STREAM_WRITABLE:
    return php_quic_stream_writable_ce;
  case PHP_QUIC_EVENT_STREAM_CLOSED:
    return php_quic_stream_closed_ce;
  case PHP_QUIC_EVENT_STREAM_RESET:
    return php_quic_stream_reset_ce;
  case PHP_QUIC_EVENT_STREAM_OPENED:
  default:
    return php_quic_stream_opened_ce;
  }
}

void php_quic_event_create_from_native(zval *return_value,
                                       const php_quic_event *event) {
  php_quic_event_object *event_obj;

  object_init_ex(return_value, php_quic_event_class_for_type(event->type));
  event_obj = Z_QUIC_EVENT_OBJ_P(return_value);
  event_obj->type = event->type;
  event_obj->stream_id = event->stream_id;
  event_obj->error_code = event->error_code;
  event_obj->by_peer = event->by_peer;
  event_obj->timestamp = event->timestamp;

  if (event_obj->reason != NULL) {
    zend_string_release(event_obj->reason);
  }
  if (event->reason != NULL) {
    event_obj->reason = zend_string_copy(event->reason);
  } else {
    event_obj->reason = zend_string_init("", 0, 0);
  }
}

int php_ngtcp2_event_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Event", php_quic_event_methods);
  php_quic_event_ce = zend_register_internal_class(&ce);
  php_quic_event_ce->create_object = php_quic_event_create_object;
  php_quic_event_ce->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "ConnectionEvent", NULL);
  php_quic_connection_event_ce =
    zend_register_internal_class_ex(&ce, php_quic_event_ce);
  php_quic_connection_event_ce->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamEvent", NULL);
  php_quic_stream_event_ce =
    zend_register_internal_class_ex(&ce, php_quic_event_ce);
  php_quic_stream_event_ce->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "TerminalStreamEvent", NULL);
  php_quic_terminal_stream_event_ce =
    zend_register_internal_class_ex(&ce, php_quic_stream_event_ce);
  php_quic_terminal_stream_event_ce->ce_flags |=
    ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "HandshakeCompleted", NULL);
  php_quic_handshake_completed_ce =
    zend_register_internal_class_ex(&ce, php_quic_connection_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "ConnectionClosed", NULL);
  php_quic_connection_closed_ce =
    zend_register_internal_class_ex(&ce, php_quic_connection_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "ConnectionDraining", NULL);
  php_quic_connection_draining_ce =
    zend_register_internal_class_ex(&ce, php_quic_connection_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamOpened", NULL);
  php_quic_stream_opened_ce =
    zend_register_internal_class_ex(&ce, php_quic_stream_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamReadable", NULL);
  php_quic_stream_readable_ce =
    zend_register_internal_class_ex(&ce, php_quic_stream_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamWritable", NULL);
  php_quic_stream_writable_ce =
    zend_register_internal_class_ex(&ce, php_quic_stream_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamClosed", NULL);
  php_quic_stream_closed_ce =
    zend_register_internal_class_ex(&ce, php_quic_terminal_stream_event_ce);

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Nghttp2\\Events", "StreamReset", NULL);
  php_quic_stream_reset_ce =
    zend_register_internal_class_ex(&ce, php_quic_terminal_stream_event_ce);

  memcpy(&php_quic_event_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_event_handlers.offset = XtOffsetOf(php_quic_event_object, std);
  php_quic_event_handlers.free_obj = php_quic_event_free_object;

  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("HANDSHAKE_COMPLETED"),
                                   PHP_QUIC_EVENT_HANDSHAKE_COMPLETED);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("CONNECTION_CLOSED"),
                                   PHP_QUIC_EVENT_CONNECTION_CLOSED);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("CONNECTION_DRAINING"),
                                   PHP_QUIC_EVENT_CONNECTION_DRAINING);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("STREAM_OPENED"),
                                   PHP_QUIC_EVENT_STREAM_OPENED);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("STREAM_READABLE"),
                                   PHP_QUIC_EVENT_STREAM_READABLE);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("STREAM_WRITABLE"),
                                   PHP_QUIC_EVENT_STREAM_WRITABLE);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("STREAM_CLOSED"),
                                   PHP_QUIC_EVENT_STREAM_CLOSED);
  zend_declare_class_constant_long(php_quic_event_ce, ZEND_STRL("STREAM_RESET"),
                                   PHP_QUIC_EVENT_STREAM_RESET);

  return SUCCESS;
}
