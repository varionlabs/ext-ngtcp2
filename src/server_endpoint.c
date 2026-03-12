#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Zend/zend_exceptions.h>
#include <Zend/zend_interfaces.h>
#include <string.h>

#include "internal/datagram.h"
#include "internal/macros.h"
#include "internal/server_connection.h"
#include "internal/server_endpoint.h"

zend_class_entry *php_quic_server_endpoint_ce;
static zend_object_handlers php_quic_server_endpoint_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_endpoint_construct, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, config, Varion\\Ngtcp2\\ServerConfig, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_endpoint_recv, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, datagram, Varion\\Ngtcp2\\Datagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_endpoint_no_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_endpoint_get_next_expiry, 0, 0,
                                        IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_endpoint_drain_array, 0, 0,
                                        IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_endpoint_get_connection_count, 0, 0,
                                        IS_LONG, 0)
ZEND_END_ARG_INFO()

static zend_string *php_quic_server_endpoint_connection_key(zval *datagram_zv) {
  php_quic_datagram *datagram = Z_QUIC_DATAGRAM_P(datagram_zv);
  php_quic_address *address;

  if (Z_TYPE(datagram->peer_address) != IS_OBJECT) {
    return NULL;
  }

  address = Z_QUIC_ADDRESS_P(&datagram->peer_address);
  if (address->host == NULL || ZSTR_LEN(address->host) == 0) {
    return NULL;
  }

  return strpprintf(0, "%s|%ld", ZSTR_VAL(address->host), address->port);
}

static int php_quic_server_endpoint_call_method(zval *object, const char *method,
                                                uint32_t method_len, zval *retval,
                                                uint32_t param_count, zval *params) {
  zval fn;
  int rv;

  ZVAL_STRINGL(&fn, method, method_len);
  rv = call_user_function(NULL, object, &fn, retval, param_count, params);
  zval_ptr_dtor(&fn);

  return rv;
}

static int php_quic_server_endpoint_call_static_accept(zval *datagram, zval *config,
                                                       zval *retval) {
  zval fn;
  zval params[2];
  int rv;

  ZVAL_STRINGL(&fn, "accept", sizeof("accept") - 1);
  ZVAL_COPY(&params[0], datagram);
  ZVAL_COPY(&params[1], config);
  rv = call_user_function(&php_quic_server_connection_ce->function_table, NULL, &fn,
                          retval, 2, params);
  zval_ptr_dtor(&params[0]);
  zval_ptr_dtor(&params[1]);
  zval_ptr_dtor(&fn);

  return rv;
}

static void php_quic_server_endpoint_cleanup_connections(php_quic_server_endpoint *endpoint) {
  zend_string *key;
  zval *conn_zv;
  HashTable remove_keys;
  zval key_zv;

  zend_hash_init(&remove_keys, 0, NULL, ZVAL_PTR_DTOR, 0);

  ZEND_HASH_FOREACH_STR_KEY_VAL(&endpoint->connections, key, conn_zv) {
    php_quic_connection *connection;

    if (key == NULL || Z_TYPE_P(conn_zv) != IS_OBJECT) {
      continue;
    }

    connection = Z_QUIC_CONNECTION_P(conn_zv);
    if (connection->closed &&
        (!connection->close_requested || connection->close_packet_sent)) {
      ZVAL_STR_COPY(&key_zv, key);
      zend_hash_next_index_insert(&remove_keys, &key_zv);
    }
  }
  ZEND_HASH_FOREACH_END();

  ZEND_HASH_FOREACH_VAL(&remove_keys, conn_zv) {
    if (Z_TYPE_P(conn_zv) == IS_STRING) {
      zend_hash_del(&endpoint->connections, Z_STR_P(conn_zv));
    }
  }
  ZEND_HASH_FOREACH_END();

  zend_hash_destroy(&remove_keys);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, __construct) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *config;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_OBJECT_OF_CLASS(config, php_quic_server_config_ce)
  ZEND_PARSE_PARAMETERS_END();

  if (!Z_ISUNDEF(endpoint->config)) {
    zval_ptr_dtor(&endpoint->config);
  }
  ZVAL_COPY(&endpoint->config, config);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, recv) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *datagram;
  zend_string *key;
  zval *conn_zv;
  zval retval;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_OBJECT_OF_CLASS(datagram, php_quic_datagram_ce)
  ZEND_PARSE_PARAMETERS_END();

  key = php_quic_server_endpoint_connection_key(datagram);
  if (key == NULL) {
    zend_throw_exception(zend_ce_exception,
                         "ServerEndpoint::recv [datagram]: Datagram.peerAddress is invalid",
                         0);
    RETURN_THROWS();
  }

  conn_zv = zend_hash_find(&endpoint->connections, key);
  if (conn_zv != NULL) {
    zval params[1];

    ZVAL_UNDEF(&retval);
    ZVAL_COPY(&params[0], datagram);
    if (php_quic_server_endpoint_call_method(conn_zv, "recv", sizeof("recv") - 1, &retval,
                                             1, params) == FAILURE) {
      zval_ptr_dtor(&params[0]);
      zend_string_release(key);
      zend_throw_exception(zend_ce_exception, "ServerEndpoint::recv: failed to route datagram",
                           0);
      RETURN_THROWS();
    }
    zval_ptr_dtor(&params[0]);
    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
    zend_string_release(key);
    php_quic_server_endpoint_cleanup_connections(endpoint);
    return;
  }

  ZVAL_UNDEF(&retval);
  if (php_quic_server_endpoint_call_static_accept(datagram, &endpoint->config, &retval) ==
      FAILURE) {
    zend_string_release(key);
    zend_throw_exception(zend_ce_exception, "ServerEndpoint::recv: accept dispatch failed", 0);
    RETURN_THROWS();
  }
  if (Z_TYPE(retval) != IS_OBJECT) {
    zend_string_release(key);
    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
    zend_throw_exception(zend_ce_exception,
                         "ServerEndpoint::recv: ServerConnection::accept returned invalid value",
                         0);
    RETURN_THROWS();
  }

  {
    zval conn_copy;
    ZVAL_COPY(&conn_copy, &retval);
    zend_hash_update(&endpoint->connections, key, &conn_copy);
  }
  add_next_index_zval(&endpoint->accepted_queue, &retval);
  zend_string_release(key);

  php_quic_server_endpoint_cleanup_connections(endpoint);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, handleTimers) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *conn_zv;
  zval retval;

  ZEND_PARSE_PARAMETERS_NONE();

  ZEND_HASH_FOREACH_VAL(&endpoint->connections, conn_zv) {
    ZVAL_UNDEF(&retval);
    if (php_quic_server_endpoint_call_method(conn_zv, "handleTimers",
                                             sizeof("handleTimers") - 1, &retval, 0,
                                             NULL) == FAILURE) {
      zend_throw_exception(zend_ce_exception,
                           "ServerEndpoint::handleTimers: timer dispatch failed", 0);
      RETURN_THROWS();
    }
    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
  }
  ZEND_HASH_FOREACH_END();

  php_quic_server_endpoint_cleanup_connections(endpoint);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, onTimeout) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_ServerEndpoint_handleTimers)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, tick) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_ServerEndpoint_handleTimers)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, getNextExpiry) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *conn_zv;
  zval retval;
  zend_long min_expiry = 0;
  zend_bool has_expiry = 0;

  ZEND_PARSE_PARAMETERS_NONE();

  ZEND_HASH_FOREACH_VAL(&endpoint->connections, conn_zv) {
    ZVAL_UNDEF(&retval);
    if (php_quic_server_endpoint_call_method(conn_zv, "getNextExpiry",
                                             sizeof("getNextExpiry") - 1, &retval, 0,
                                             NULL) == FAILURE) {
      zend_throw_exception(zend_ce_exception,
                           "ServerEndpoint::getNextExpiry: deadline dispatch failed", 0);
      RETURN_THROWS();
    }

    if (Z_TYPE(retval) == IS_LONG) {
      if (!has_expiry || Z_LVAL(retval) < min_expiry) {
        min_expiry = Z_LVAL(retval);
      }
      has_expiry = 1;
    }

    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
  }
  ZEND_HASH_FOREACH_END();

  if (!has_expiry) {
    RETURN_NULL();
  }

  RETURN_LONG(min_expiry);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, getTimeoutAt) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_ServerEndpoint_getNextExpiry)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, getNextTimeout) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *conn_zv;
  zval retval;
  zend_long min_timeout = 0;
  zend_bool has_timeout = 0;

  ZEND_PARSE_PARAMETERS_NONE();

  ZEND_HASH_FOREACH_VAL(&endpoint->connections, conn_zv) {
    ZVAL_UNDEF(&retval);
    if (php_quic_server_endpoint_call_method(conn_zv, "getNextTimeout",
                                             sizeof("getNextTimeout") - 1, &retval, 0,
                                             NULL) == FAILURE) {
      zend_throw_exception(zend_ce_exception,
                           "ServerEndpoint::getNextTimeout: timeout dispatch failed", 0);
      RETURN_THROWS();
    }

    if (Z_TYPE(retval) == IS_LONG) {
      if (!has_timeout || Z_LVAL(retval) < min_timeout) {
        min_timeout = Z_LVAL(retval);
      }
      has_timeout = 1;
    }

    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
  }
  ZEND_HASH_FOREACH_END();

  if (!has_timeout) {
    RETURN_NULL();
  }

  RETURN_LONG(min_timeout);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, drainAcceptedConnections) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);

  ZEND_PARSE_PARAMETERS_NONE();

  RETVAL_COPY(&endpoint->accepted_queue);
  zval_ptr_dtor(&endpoint->accepted_queue);
  array_init(&endpoint->accepted_queue);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, drainOutgoingDatagrams) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *conn_zv;
  zval retval;
  zval *item;

  ZEND_PARSE_PARAMETERS_NONE();
  array_init(return_value);

  ZEND_HASH_FOREACH_VAL(&endpoint->connections, conn_zv) {
    ZVAL_UNDEF(&retval);
    if (php_quic_server_endpoint_call_method(conn_zv, "drainOutgoingDatagrams",
                                             sizeof("drainOutgoingDatagrams") - 1, &retval,
                                             0, NULL) == FAILURE) {
      zval_ptr_dtor(return_value);
      zend_throw_exception(zend_ce_exception,
                           "ServerEndpoint::drainOutgoingDatagrams: drain dispatch failed",
                           0);
      RETURN_THROWS();
    }

    if (Z_TYPE(retval) == IS_ARRAY) {
      ZEND_HASH_FOREACH_VAL(Z_ARRVAL(retval), item) {
        Z_TRY_ADDREF_P(item);
        add_next_index_zval(return_value, item);
      }
      ZEND_HASH_FOREACH_END();
    }

    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
  }
  ZEND_HASH_FOREACH_END();

  php_quic_server_endpoint_cleanup_connections(endpoint);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, drainEvents) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  zval *conn_zv;
  zval retval;
  zval *item;

  ZEND_PARSE_PARAMETERS_NONE();
  array_init(return_value);

  ZEND_HASH_FOREACH_VAL(&endpoint->connections, conn_zv) {
    ZVAL_UNDEF(&retval);
    if (php_quic_server_endpoint_call_method(conn_zv, "drainEvents",
                                             sizeof("drainEvents") - 1, &retval, 0, NULL) ==
        FAILURE) {
      zval_ptr_dtor(return_value);
      zend_throw_exception(zend_ce_exception,
                           "ServerEndpoint::drainEvents: drain dispatch failed", 0);
      RETURN_THROWS();
    }

    if (Z_TYPE(retval) == IS_ARRAY) {
      ZEND_HASH_FOREACH_VAL(Z_ARRVAL(retval), item) {
        Z_TRY_ADDREF_P(item);
        add_next_index_zval(return_value, item);
      }
      ZEND_HASH_FOREACH_END();
    }

    if (!Z_ISUNDEF(retval)) {
      zval_ptr_dtor(&retval);
    }
  }
  ZEND_HASH_FOREACH_END();

  php_quic_server_endpoint_cleanup_connections(endpoint);
}

PHP_METHOD(Ngtcp2_ServerEndpoint, getConnectionCount) {
  php_quic_server_endpoint *endpoint = Z_QUIC_SERVER_ENDPOINT_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_LONG(zend_hash_num_elements(&endpoint->connections));
}

static const zend_function_entry php_quic_server_endpoint_methods[] = {
  PHP_ME(Ngtcp2_ServerEndpoint, __construct, arginfo_server_endpoint_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, recv, arginfo_server_endpoint_recv, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, handleTimers, arginfo_server_endpoint_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, onTimeout, arginfo_server_endpoint_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, tick, arginfo_server_endpoint_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, getNextExpiry, arginfo_server_endpoint_get_next_expiry,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, getTimeoutAt, arginfo_server_endpoint_get_next_expiry,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, getNextTimeout, arginfo_server_endpoint_get_next_expiry,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, drainAcceptedConnections, arginfo_server_endpoint_drain_array,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, drainOutgoingDatagrams, arginfo_server_endpoint_drain_array,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, drainEvents, arginfo_server_endpoint_drain_array,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerEndpoint, getConnectionCount, arginfo_server_endpoint_get_connection_count,
         ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_server_endpoint_create_object(zend_class_entry *ce) {
  php_quic_server_endpoint *endpoint;

  endpoint = zend_object_alloc(sizeof(*endpoint), ce);
  ZVAL_UNDEF(&endpoint->config);
  array_init(&endpoint->accepted_queue);
  zend_hash_init(&endpoint->connections, 0, NULL, ZVAL_PTR_DTOR, 0);

  zend_object_std_init(&endpoint->std, ce);
  object_properties_init(&endpoint->std, ce);
  endpoint->std.handlers = &php_quic_server_endpoint_handlers;

  return &endpoint->std;
}

static void php_quic_server_endpoint_free_object(zend_object *object) {
  php_quic_server_endpoint *endpoint;

  endpoint = (php_quic_server_endpoint *)((char *)object -
                                          XtOffsetOf(php_quic_server_endpoint, std));

  if (!Z_ISUNDEF(endpoint->config)) {
    zval_ptr_dtor(&endpoint->config);
    ZVAL_UNDEF(&endpoint->config);
  }
  if (!Z_ISUNDEF(endpoint->accepted_queue)) {
    zval_ptr_dtor(&endpoint->accepted_queue);
    ZVAL_UNDEF(&endpoint->accepted_queue);
  }
  zend_hash_destroy(&endpoint->connections);

  zend_object_std_dtor(&endpoint->std);
}

int php_ngtcp2_server_endpoint_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "ServerEndpoint",
                      php_quic_server_endpoint_methods);
  php_quic_server_endpoint_ce = zend_register_internal_class(&ce);
  php_quic_server_endpoint_ce->create_object = php_quic_server_endpoint_create_object;

  memcpy(&php_quic_server_endpoint_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_server_endpoint_handlers.offset =
    XtOffsetOf(php_quic_server_endpoint, std);
  php_quic_server_endpoint_handlers.free_obj = php_quic_server_endpoint_free_object;

  return SUCCESS;
}
