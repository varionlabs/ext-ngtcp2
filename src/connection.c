#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <inttypes.h>
#include <sys/time.h>
#include <string.h>

#include <Zend/zend_exceptions.h>
#include <Zend/zend_hrtime.h>

#include <gnutls/crypto.h>

#include <ngtcp2/ngtcp2_crypto.h>

#include "internal/address.h"
#include "internal/callbacks.h"
#include "internal/connection.h"
#include "internal/datagram.h"
#include "internal/event.h"
#include "internal/macros.h"
#include "internal/queue.h"
#include "internal/stream.h"
#include "internal/tls.h"

zend_class_entry *php_quic_connection_ce;
static zend_class_entry *php_quic_client_config_ce;
static zend_object_handlers php_quic_connection_handlers;
static zend_object_handlers php_quic_client_config_handlers;
static zend_bool php_quic_connection_is_nonfatal_closing_error(int rv);
static void php_quic_connection_sync_state(php_quic_connection *connection);
static void php_quic_connection_throw_ngtcp2_error(int rv, const char *context);

static ngtcp2_tstamp php_quic_timestamp(void) {
  return (ngtcp2_tstamp)zend_hrtime();
}

static zend_long php_quic_epoch_milliseconds(void) {
  struct timeval tv;

  if (gettimeofday(&tv, NULL) != 0) {
    return 0;
  }

  return (zend_long)tv.tv_sec * 1000 + (zend_long)(tv.tv_usec / 1000);
}

static zend_bool php_quic_connection_get_next_expiry(php_quic_connection *connection,
                                                     ngtcp2_tstamp *now,
                                                     ngtcp2_tstamp *expiry) {
  if (connection->conn == NULL || connection->closed) {
    return 0;
  }

  *now = php_quic_timestamp();
  *expiry = ngtcp2_conn_get_expiry(connection->conn);
  return 1;
}

static zend_long php_quic_connection_get_next_timeout_ms(php_quic_connection *connection,
                                                         zend_bool *has_timeout) {
  ngtcp2_tstamp now;
  ngtcp2_tstamp expiry;

  if (!php_quic_connection_get_next_expiry(connection, &now, &expiry)) {
    *has_timeout = 0;
    return 0;
  }

  *has_timeout = 1;
  if (expiry <= now) {
    return 0;
  }

  return (zend_long)((expiry - now) / NGTCP2_MILLISECONDS);
}

static int php_quic_connection_handle_timers(php_quic_connection *connection) {
  int rv;

  if (connection->conn == NULL || connection->closed) {
    return SUCCESS;
  }

  rv = ngtcp2_conn_handle_expiry(connection->conn, php_quic_timestamp());
  if (rv != 0) {
    if (php_quic_connection_is_nonfatal_closing_error(rv)) {
      php_quic_connection_sync_state(connection);
      return SUCCESS;
    }

    ngtcp2_ccerr_set_liberr(&connection->last_error, rv, NULL, 0);
    php_quic_connection_throw_ngtcp2_error(rv, "ngtcp2_conn_handle_expiry");
    return FAILURE;
  }

  php_quic_connection_sync_state(connection);
  return SUCCESS;
}

static zend_bool php_quic_connection_is_nonfatal_closing_error(int rv) {
  return rv == NGTCP2_ERR_CLOSING || rv == NGTCP2_ERR_DRAINING;
}

static ngtcp2_conn *php_quic_get_conn(ngtcp2_crypto_conn_ref *conn_ref) {
  php_quic_connection *connection = conn_ref->user_data;
  return connection->conn;
}

static int php_quic_fill_random(uint8_t *dest, size_t destlen) {
  if (gnutls_rnd(GNUTLS_RND_NONCE, dest, destlen) != 0) {
    return FAILURE;
  }

  return SUCCESS;
}

static int php_quic_is_numeric_host(const char *host) {
  struct in_addr addr4;
  struct in6_addr addr6;

  return inet_pton(AF_INET, host, &addr4) == 1 ||
         inet_pton(AF_INET6, host, &addr6) == 1;
}

static int php_quic_default_local_addr(int family, struct sockaddr_storage *storage,
                                       socklen_t *addrlen) {
  memset(storage, 0, sizeof(*storage));

  if (family == AF_INET6) {
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)storage;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(0);
    *addrlen = sizeof(*sin6);
    return SUCCESS;
  }

  {
    struct sockaddr_in *sin = (struct sockaddr_in *)storage;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(0);
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    *addrlen = sizeof(*sin);
  }

  return SUCCESS;
}

static void php_quic_set_nullable_string(zend_string **target, zend_string *value) {
  if (*target != NULL) {
    zend_string_release(*target);
    *target = NULL;
  }

  if (value != NULL) {
    *target = zend_string_copy(value);
  }
}

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
  entry->tx_offset = 0;
  entry->readable = 0;
  entry->writable = 1;
  entry->closed = 0;
  entry->reset = 0;
  entry->fin_sent = 0;
  entry->fin_recv = 0;
  entry->fin_pending = 0;
  entry->pending_open = 0;

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

static void php_quic_connection_sync_state(php_quic_connection *connection) {
  const ngtcp2_ccerr *ccerr;

  if (connection->conn == NULL) {
    return;
  }

  if (ngtcp2_conn_in_draining_period(connection->conn)) {
    connection->draining = 1;
    if (!connection->draining_event_emitted) {
      connection->draining_event_emitted = 1;
      php_quic_connection_push_event(connection, PHP_QUIC_EVENT_CONNECTION_DRAINING, -1, 0, 1,
                                     NULL);
    }
  }

  if (ngtcp2_conn_in_closing_period(connection->conn) ||
      ngtcp2_conn_in_draining_period(connection->conn)) {
    connection->closed = 1;
    if (!connection->close_event_emitted) {
      ccerr = ngtcp2_conn_get_ccerr(connection->conn);
      php_quic_connection_push_event(connection, PHP_QUIC_EVENT_CONNECTION_CLOSED, -1,
                                     ccerr != NULL ? ccerr->error_code : 0, 1, NULL);
      connection->close_event_emitted = 1;
    }
  }
}

static php_quic_stream_entry *php_quic_connection_pick_tx_stream(
  php_quic_connection *connection, int64_t *stream_id, ngtcp2_vec *datav,
  size_t *datavcnt, int *fin) {
  php_quic_stream_entry *entry;
  zval *zv;

  ZEND_HASH_FOREACH_VAL(&connection->streams, zv) {
    entry = Z_PTR_P(zv);
    if (entry == NULL || entry->closed || entry->reset) {
      continue;
    }

    if (entry->tx_offset < ZSTR_LEN(entry->tx_buffer) || entry->fin_pending) {
      if (entry->tx_offset > ZSTR_LEN(entry->tx_buffer)) {
        entry->tx_offset = ZSTR_LEN(entry->tx_buffer);
      }

      *stream_id = entry->stream_id;
      *fin = entry->fin_pending ? 1 : 0;

      if (entry->tx_offset < ZSTR_LEN(entry->tx_buffer)) {
        datav->base = (uint8_t *)(ZSTR_VAL(entry->tx_buffer) + entry->tx_offset);
        datav->len = ZSTR_LEN(entry->tx_buffer) - entry->tx_offset;
        *datavcnt = 1;
      } else {
        datav->base = NULL;
        datav->len = 0;
        *datavcnt = 0;
      }

      return entry;
    }
  }
  ZEND_HASH_FOREACH_END();

  *stream_id = -1;
  *fin = 0;
  datav->base = NULL;
  datav->len = 0;
  *datavcnt = 0;

  return NULL;
}

static void php_quic_connection_update_tx_entry(php_quic_connection *connection,
                                                 php_quic_stream_entry *entry,
                                                 ngtcp2_ssize wdatalen,
                                                 zend_bool fin_attempted,
                                                 zend_bool packet_emitted) {
  size_t tx_len;

  if (entry == NULL) {
    return;
  }

  if (wdatalen > 0) {
    entry->tx_offset += (uint64_t)wdatalen;
  }

  tx_len = ZSTR_LEN(entry->tx_buffer);
  if (entry->tx_offset > tx_len) {
    entry->tx_offset = tx_len;
  }

  if (fin_attempted && packet_emitted && entry->fin_pending && entry->tx_offset >= tx_len) {
    entry->fin_pending = 0;
    entry->fin_sent = 1;
    entry->writable = 0;
  }

  if (entry->tx_offset >= tx_len && tx_len > 0) {
    zend_string_release(entry->tx_buffer);
    entry->tx_buffer = zend_string_init("", 0, 0);
    entry->tx_offset = 0;
  }

  if (!entry->fin_pending && !entry->fin_sent && !entry->closed && !entry->writable &&
      entry->tx_offset == 0 && ZSTR_LEN(entry->tx_buffer) == 0) {
    entry->writable = 1;
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE,
                                   entry->stream_id, 0, 0, NULL);
  }
}

static void php_quic_connection_discard_tx_entry(php_quic_stream_entry *entry) {
  if (entry == NULL) {
    return;
  }

  if (entry->tx_buffer != NULL) {
    zend_string_release(entry->tx_buffer);
    entry->tx_buffer = zend_string_init("", 0, 0);
  }
  entry->tx_offset = 0;
  entry->fin_pending = 0;
  entry->fin_sent = 1;
  entry->writable = 0;
}

static int php_quic_connection_init_native(php_quic_connection *connection) {
  ngtcp2_callbacks callbacks;
  ngtcp2_settings settings;
  ngtcp2_transport_params params;
  ngtcp2_path path;
  ngtcp2_cid dcid;
  ngtcp2_cid scid;
  int rv;

  php_quic_callbacks_init(&callbacks);

  dcid.datalen = NGTCP2_MIN_INITIAL_DCIDLEN;
  if (php_quic_fill_random(dcid.data, dcid.datalen) != SUCCESS) {
    php_error_docref(NULL, E_WARNING, "failed to generate random DCID");
    return FAILURE;
  }

  scid.datalen = 8;
  if (php_quic_fill_random(scid.data, scid.datalen) != SUCCESS) {
    php_error_docref(NULL, E_WARNING, "failed to generate random SCID");
    return FAILURE;
  }

  ngtcp2_settings_default(&settings);
  settings.initial_ts = php_quic_timestamp();

  ngtcp2_transport_params_default(&params);
  params.initial_max_streams_bidi = 16;
  params.initial_max_streams_uni = 3;
  params.initial_max_stream_data_bidi_local = 128 * 1024;
  params.initial_max_stream_data_bidi_remote = 128 * 1024;
  params.initial_max_data = 1024 * 1024;
  params.max_idle_timeout = 30 * NGTCP2_SECONDS;

  memset(&path, 0, sizeof(path));
  path.local.addr = (struct sockaddr *)&connection->local_addr;
  path.local.addrlen = connection->local_addrlen;
  path.remote.addr = (struct sockaddr *)&connection->remote_addr;
  path.remote.addrlen = connection->remote_addrlen;

  rv = ngtcp2_conn_client_new(&connection->conn, &dcid, &scid, &path,
                              NGTCP2_PROTO_VER_V1, &callbacks, &settings, &params,
                              NULL, connection);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "ngtcp2_conn_client_new failed: %s",
                     ngtcp2_strerror(rv));
    return FAILURE;
  }

  ngtcp2_conn_set_tls_native_handle(connection->conn, connection->session);
  return SUCCESS;
}

static void php_quic_connection_throw_ngtcp2_error(int rv, const char *context) {
  zend_throw_exception_ex(zend_ce_exception, 0, "%s failed: %s", context,
                          ngtcp2_strerror(rv));
}

static void php_quic_connection_ensure_next_stream_id(
  php_quic_connection *connection, zend_bool bidi) {
  int64_t minimum_stream_id;
  int64_t *next_stream_id;

  if (connection->conn != NULL && ngtcp2_conn_is_server(connection->conn)) {
    minimum_stream_id = bidi ? 1 : 3;
  } else {
    minimum_stream_id = bidi ? 0 : 2;
  }

  next_stream_id = bidi ? &connection->next_bidi_stream_id :
                          &connection->next_uni_stream_id;

  if (*next_stream_id < minimum_stream_id ||
      ((*next_stream_id & 0x3) != (minimum_stream_id & 0x3))) {
    *next_stream_id = minimum_stream_id;
  }
}

static const char *php_quic_connection_open_context_for_stream_id(
  int64_t stream_id) {
  if (ngtcp2_is_bidi_stream(stream_id)) {
    return "ngtcp2_conn_open_bidi_stream";
  }

  return "ngtcp2_conn_open_uni_stream";
}

static int php_quic_connection_open_pending_stream(php_quic_connection *connection,
                                                   int64_t stream_id,
                                                   int64_t *opened_stream_id) {
  if (ngtcp2_is_bidi_stream(stream_id)) {
    return ngtcp2_conn_open_bidi_stream(connection->conn, opened_stream_id, NULL);
  }

  return ngtcp2_conn_open_uni_stream(connection->conn, opened_stream_id, NULL);
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_client_config_construct, 0, 0, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, serverName, IS_STRING, 1, "null")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, alpn, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_client_config_with_local_address, 0, 1,
                                       Varion\\Ngtcp2\\ClientConfig, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_client_config_with_server_name, 0, 1,
                                       Varion\\Ngtcp2\\ClientConfig, 0)
  ZEND_ARG_TYPE_INFO(0, serverName, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_client_config_with_alpn, 0, 1,
                                       Varion\\Ngtcp2\\ClientConfig, 0)
  ZEND_ARG_TYPE_INFO(0, alpn, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_client_config_get_local_address, 0, 0,
                                       Varion\\Ngtcp2\\Address, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_client_config_get_server_name, 0, 0,
                                        IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_client_config_get_alpn, 0, 0, IS_STRING,
                                        1)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_ClientConfig, __construct) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);
  zval *local_address = NULL;
  zend_string *server_name = NULL;
  zend_string *alpn = NULL;

  ZEND_PARSE_PARAMETERS_START(0, 3)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_STR_OR_NULL(server_name)
    Z_PARAM_STR_OR_NULL(alpn)
  ZEND_PARSE_PARAMETERS_END();

  if (server_name != NULL && ZSTR_LEN(server_name) == 0) {
    zend_argument_value_error(2, "must be a non-empty string or null");
    RETURN_THROWS();
  }

  if (alpn != NULL && ZSTR_LEN(alpn) == 0) {
    zend_argument_value_error(3, "must be a non-empty string or null");
    RETURN_THROWS();
  }

  if (!Z_ISUNDEF(config->local_address)) {
    zval_ptr_dtor(&config->local_address);
  }
  if (local_address != NULL) {
    ZVAL_COPY(&config->local_address, local_address);
  } else {
    ZVAL_NULL(&config->local_address);
  }

  php_quic_set_nullable_string(&config->server_name, server_name);
  php_quic_set_nullable_string(&config->alpn, alpn);
}

PHP_METHOD(Ngtcp2_ClientConfig, withLocalAddress) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);
  zval *local_address = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
  ZEND_PARSE_PARAMETERS_END();

  if (!Z_ISUNDEF(config->local_address)) {
    zval_ptr_dtor(&config->local_address);
  }
  if (local_address != NULL) {
    ZVAL_COPY(&config->local_address, local_address);
  } else {
    ZVAL_NULL(&config->local_address);
  }

  RETURN_ZVAL(ZEND_THIS, 1, 0);
}

PHP_METHOD(Ngtcp2_ClientConfig, withServerName) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);
  zend_string *server_name = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STR_OR_NULL(server_name)
  ZEND_PARSE_PARAMETERS_END();

  if (server_name != NULL && ZSTR_LEN(server_name) == 0) {
    zend_argument_value_error(1, "must be a non-empty string or null");
    RETURN_THROWS();
  }

  php_quic_set_nullable_string(&config->server_name, server_name);
  RETURN_ZVAL(ZEND_THIS, 1, 0);
}

PHP_METHOD(Ngtcp2_ClientConfig, withAlpn) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);
  zend_string *alpn = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STR_OR_NULL(alpn)
  ZEND_PARSE_PARAMETERS_END();

  if (alpn != NULL && ZSTR_LEN(alpn) == 0) {
    zend_argument_value_error(1, "must be a non-empty string or null");
    RETURN_THROWS();
  }

  php_quic_set_nullable_string(&config->alpn, alpn);
  RETURN_ZVAL(ZEND_THIS, 1, 0);
}

PHP_METHOD(Ngtcp2_ClientConfig, getLocalAddress) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);
  RETURN_COPY(&config->local_address);
}

PHP_METHOD(Ngtcp2_ClientConfig, getServerName) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);

  if (config->server_name == NULL) {
    RETURN_NULL();
  }

  RETURN_STR_COPY(config->server_name);
}

PHP_METHOD(Ngtcp2_ClientConfig, getAlpn) {
  php_quic_client_config *config = Z_QUIC_CLIENT_CONFIG_P(ZEND_THIS);

  if (config->alpn == NULL) {
    RETURN_NULL();
  }

  RETURN_STR_COPY(config->alpn);
}

static const zend_function_entry php_quic_client_config_methods[] = {
  PHP_ME(Ngtcp2_ClientConfig, __construct, arginfo_client_config_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, withLocalAddress, arginfo_client_config_with_local_address,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, withServerName, arginfo_client_config_with_server_name,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, withAlpn, arginfo_client_config_with_alpn, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, getLocalAddress, arginfo_client_config_get_local_address,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, getServerName, arginfo_client_config_get_server_name,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ClientConfig, getAlpn, arginfo_client_config_get_alpn, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_client_config_create_object(zend_class_entry *ce) {
  php_quic_client_config *config;

  config = zend_object_alloc(sizeof(*config), ce);
  ZVAL_UNDEF(&config->local_address);
  ZVAL_NULL(&config->local_address);
  config->server_name = NULL;
  config->alpn = NULL;

  zend_object_std_init(&config->std, ce);
  object_properties_init(&config->std, ce);
  config->std.handlers = &php_quic_client_config_handlers;

  return &config->std;
}

static void php_quic_client_config_free_object(zend_object *object) {
  php_quic_client_config *config;

  config = (php_quic_client_config *)((char *)object -
                                      XtOffsetOf(php_quic_client_config, std));

  if (!Z_ISUNDEF(config->local_address)) {
    zval_ptr_dtor(&config->local_address);
    ZVAL_UNDEF(&config->local_address);
  }

  php_quic_set_nullable_string(&config->server_name, NULL);
  php_quic_set_nullable_string(&config->alpn, NULL);

  zend_object_std_dtor(&config->std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_construct, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, remoteAddress, Varion\\Ngtcp2\\Address, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, config, Varion\\Ngtcp2\\ClientConfig, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_recv, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, datagram, Varion\\Ngtcp2\\Datagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_no_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_get_next_timeout, 0, 0,
                                        IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_get_next_expiry, 0, 0,
                                        IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_drain_events, 0, 0,
                                        IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_connection_drain_outgoing_datagrams, 0, 0,
                                        IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_connection_open_stream, 0, 0,
                                       Varion\\Ngtcp2\\Stream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_connection_open_uni_stream, 0, 0,
                                       Varion\\Ngtcp2\\Stream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_connection_get_stream, 0, 1,
                                       Varion\\Ngtcp2\\Stream, 1)
  ZEND_ARG_TYPE_INFO(0, streamId, IS_LONG, 0)
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
  zval *config_zv = NULL;
  php_quic_client_config *config = NULL;
  zval *local_address = NULL;
  php_quic_address *remote_address_obj;
  const char *sni = NULL;
  const char *alpn = "h3";
  int rv;

  ZEND_PARSE_PARAMETERS_START(1, 2)
    Z_PARAM_OBJECT_OF_CLASS(remote_address, php_quic_address_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(config_zv, php_quic_client_config_ce)
  ZEND_PARSE_PARAMETERS_END();

  if (config_zv != NULL) {
    config = Z_QUIC_CLIENT_CONFIG_P(config_zv);
    if (Z_TYPE(config->local_address) == IS_OBJECT) {
      local_address = &config->local_address;
    }
    if (config->alpn != NULL) {
      alpn = ZSTR_VAL(config->alpn);
    }
  }

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);

  if (!Z_ISUNDEF(connection->remote_address_zv)) {
    zval_ptr_dtor(&connection->remote_address_zv);
  }
  ZVAL_COPY(&connection->remote_address_zv, remote_address);

  if (!Z_ISUNDEF(connection->local_address_zv)) {
    zval_ptr_dtor(&connection->local_address_zv);
  }
  if (local_address != NULL) {
    ZVAL_COPY(&connection->local_address_zv, local_address);
  } else {
    ZVAL_NULL(&connection->local_address_zv);
  }

  if (php_quic_address_to_sockaddr(&connection->remote_address_zv,
                                   &connection->remote_addr,
                                   &connection->remote_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Invalid remote address", 0);
    RETURN_THROWS();
  }

  if (local_address != NULL) {
    if (php_quic_address_to_sockaddr(&connection->local_address_zv,
                                     &connection->local_addr,
                                     &connection->local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "Invalid local address", 0);
      RETURN_THROWS();
    }
  } else if (php_quic_default_local_addr(connection->remote_addr.ss_family,
                                         &connection->local_addr,
                                         &connection->local_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Failed to initialize local address", 0);
    RETURN_THROWS();
  }

  connection->conn_ref.get_conn = php_quic_get_conn;
  connection->conn_ref.user_data = connection;

  if (connection->session == NULL &&
      php_quic_tls_gnutls_init_client(connection, alpn) != SUCCESS) {
    zend_throw_exception(zend_ce_exception,
                         "Failed to initialize GnuTLS for QUIC connection", 0);
    RETURN_THROWS();
  }

  if (config != NULL && config->server_name != NULL) {
    sni = ZSTR_VAL(config->server_name);
  } else {
    remote_address_obj = Z_QUIC_ADDRESS_P(&connection->remote_address_zv);
    sni = ZSTR_VAL(remote_address_obj->host);
    if (php_quic_is_numeric_host(sni)) {
      sni = "localhost";
    }
  }

  if (sni != NULL && sni[0] != '\0') {
    rv = gnutls_server_name_set(connection->session, GNUTLS_NAME_DNS,
                                sni, strlen(sni));
    if (rv != 0) {
      zend_throw_exception_ex(zend_ce_exception, 0,
                              "gnutls_server_name_set failed: %s",
                              gnutls_strerror(rv));
      RETURN_THROWS();
    }
  }

  if (php_quic_connection_init_native(connection) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Failed to initialize ngtcp2 connection", 0);
    RETURN_THROWS();
  }
}

PHP_METHOD(Ngtcp2_Connection, recv) {
  php_quic_connection *connection;
  php_quic_datagram *datagram;
  struct sockaddr_storage remote_addr;
  struct sockaddr_storage local_addr;
  socklen_t remote_addrlen;
  socklen_t local_addrlen;
  ngtcp2_path path;
  ngtcp2_pkt_info pi = {0};
  int rv;

  zval *datagram_zv;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_OBJECT_OF_CLASS(datagram_zv, php_quic_datagram_ce)
  ZEND_PARSE_PARAMETERS_END();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->conn == NULL) {
    zend_throw_exception(zend_ce_exception, "Connection is not initialized", 0);
    RETURN_THROWS();
  }

  datagram = Z_QUIC_DATAGRAM_P(datagram_zv);

  if (php_quic_address_to_sockaddr(&datagram->peer_address, &remote_addr,
                                   &remote_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Datagram.peerAddress is invalid", 0);
    RETURN_THROWS();
  }

  if (!Z_ISUNDEF(datagram->local_address) && Z_TYPE(datagram->local_address) == IS_OBJECT) {
    if (php_quic_address_to_sockaddr(&datagram->local_address, &local_addr,
                                     &local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "Datagram.localAddress is invalid", 0);
      RETURN_THROWS();
    }
  } else {
    local_addr = connection->local_addr;
    local_addrlen = connection->local_addrlen;
  }

  memset(&path, 0, sizeof(path));
  path.local.addr = (struct sockaddr *)&local_addr;
  path.local.addrlen = local_addrlen;
  path.remote.addr = (struct sockaddr *)&remote_addr;
  path.remote.addrlen = remote_addrlen;

  rv = ngtcp2_conn_read_pkt(connection->conn, &path, &pi,
                            (const uint8_t *)ZSTR_VAL(datagram->payload),
                            ZSTR_LEN(datagram->payload), php_quic_timestamp());
  if (rv != 0) {
    if (php_quic_connection_is_nonfatal_closing_error(rv)) {
      php_quic_connection_sync_state(connection);
      return;
    }

    if (!connection->last_error.error_code) {
      if (rv == NGTCP2_ERR_CRYPTO) {
        ngtcp2_ccerr_set_tls_alert(&connection->last_error,
                                   ngtcp2_conn_get_tls_alert(connection->conn),
                                   NULL, 0);
      } else {
        ngtcp2_ccerr_set_liberr(&connection->last_error, rv, NULL, 0);
      }
    }

    php_quic_connection_throw_ngtcp2_error(rv, "ngtcp2_conn_read_pkt");
    RETURN_THROWS();
  }

  php_quic_connection_sync_state(connection);
}

PHP_METHOD(Ngtcp2_Connection, onTimeout) {
  php_quic_connection *connection;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (php_quic_connection_handle_timers(connection) != SUCCESS) {
    RETURN_THROWS();
  }
}

PHP_METHOD(Ngtcp2_Connection, getNextTimeout) {
  php_quic_connection *connection;
  zend_bool has_timeout;
  zend_long timeout_ms;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  timeout_ms = php_quic_connection_get_next_timeout_ms(connection, &has_timeout);
  if (!has_timeout) {
    RETURN_NULL();
  }

  RETURN_LONG(timeout_ms);
}

PHP_METHOD(Ngtcp2_Connection, getNextExpiry) {
  php_quic_connection *connection;
  zend_bool has_timeout;
  zend_long timeout_ms;
  zend_long now_ms;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  timeout_ms = php_quic_connection_get_next_timeout_ms(connection, &has_timeout);
  if (!has_timeout) {
    RETURN_NULL();
  }

  now_ms = php_quic_epoch_milliseconds();
  RETURN_LONG(now_ms + timeout_ms);
}

PHP_METHOD(Ngtcp2_Connection, getTimeoutAt) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_Connection_getNextExpiry)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_Connection, handleTimers) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_Connection_onTimeout)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_Connection, tick) {
  ZEND_PARSE_PARAMETERS_NONE();
  ZEND_MN(Ngtcp2_Connection_onTimeout)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(Ngtcp2_Connection, drainEvents) {
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

PHP_METHOD(Ngtcp2_Connection, drainOutgoingDatagrams) {
  php_quic_connection *connection;
  uint8_t buf[1452];
  ngtcp2_path_storage ps;
  ngtcp2_pkt_info pi;
  ngtcp2_vec datav;
  ngtcp2_ssize nwrite;
  ngtcp2_ssize wdatalen;
  int64_t stream_id;
  uint32_t flags;
  size_t datavcnt;
  int fin;
  php_quic_stream_entry *entry;
  zval datagram_zv;
  php_quic_datagram *datagram;
  int emitted = 0;
  zend_bool force_conn_only = 0;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  array_init(return_value);

  if (connection->conn == NULL) {
    return;
  }

  if (connection->close_requested && !connection->close_packet_sent) {
    ngtcp2_path_storage_zero(&ps);
    memset(&pi, 0, sizeof(pi));

    nwrite = ngtcp2_conn_write_connection_close(
      connection->conn, &ps.path, &pi, buf, sizeof(buf), &connection->last_error,
      php_quic_timestamp());

    if (nwrite < 0) {
      if (php_quic_connection_is_nonfatal_closing_error((int)nwrite)) {
        connection->close_packet_sent = 1;
        php_quic_connection_sync_state(connection);
        return;
      }

      php_quic_connection_throw_ngtcp2_error((int)nwrite,
                                             "ngtcp2_conn_write_connection_close");
      RETURN_THROWS();
    }

    if (nwrite > 0) {
      object_init_ex(&datagram_zv, php_quic_datagram_ce);
      datagram = Z_QUIC_DATAGRAM_P(&datagram_zv);

      if (datagram->payload != NULL) {
        zend_string_release(datagram->payload);
      }
      datagram->payload = zend_string_init((const char *)buf, (size_t)nwrite, 0);

      if (!Z_ISUNDEF(datagram->peer_address)) {
        zval_ptr_dtor(&datagram->peer_address);
      }
      ZVAL_COPY(&datagram->peer_address, &connection->remote_address_zv);

      if (!Z_ISUNDEF(datagram->local_address)) {
        zval_ptr_dtor(&datagram->local_address);
      }
      ZVAL_COPY(&datagram->local_address, &connection->local_address_zv);

      add_next_index_zval(return_value, &datagram_zv);
    }

    connection->close_packet_sent = 1;
    return;
  }

  for (;;) {
    ngtcp2_path_storage_zero(&ps);
    memset(&pi, 0, sizeof(pi));

    if (force_conn_only) {
      entry = NULL;
      stream_id = -1;
      datav.base = NULL;
      datav.len = 0;
      datavcnt = 0;
      fin = 0;
      force_conn_only = 0;
    } else {
      entry = php_quic_connection_pick_tx_stream(connection, &stream_id, &datav,
                                                 &datavcnt, &fin);
    }

    if (entry != NULL && entry->pending_open) {
      int64_t opened_stream_id = -1;
      int open_rv;
      const char *open_context =
        php_quic_connection_open_context_for_stream_id(entry->stream_id);

      open_rv = php_quic_connection_open_pending_stream(connection,
                                                        entry->stream_id,
                                                        &opened_stream_id);
      if (open_rv == 0) {
        if (opened_stream_id != entry->stream_id) {
          zend_throw_exception_ex(zend_ce_exception, 0,
                                  "unexpected stream id mismatch: expected=%" PRId64
                                  " actual=%" PRId64,
                                  entry->stream_id, opened_stream_id);
          RETURN_THROWS();
        }
        entry->pending_open = 0;
      } else if (open_rv == NGTCP2_ERR_STREAM_ID_BLOCKED) {
        entry = NULL;
        stream_id = -1;
        datav.base = NULL;
        datav.len = 0;
        datavcnt = 0;
        fin = 0;
      } else {
        php_quic_connection_throw_ngtcp2_error(open_rv, open_context);
        RETURN_THROWS();
      }
    }

    flags = NGTCP2_WRITE_STREAM_FLAG_MORE;
    if (fin) {
      flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
    }

    nwrite = ngtcp2_conn_writev_stream(connection->conn, &ps.path, &pi, buf,
                                       sizeof(buf), &wdatalen, flags,
                                       stream_id,
                                       datavcnt > 0 ? &datav : NULL,
                                       datavcnt, php_quic_timestamp());

    if (nwrite < 0) {
      if (nwrite == NGTCP2_ERR_WRITE_MORE) {
        php_quic_connection_update_tx_entry(connection, entry, wdatalen,
                                            fin != 0, 0);
        continue;
      }
      if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
        php_quic_connection_update_tx_entry(connection, entry, wdatalen,
                                            fin != 0, 0);
        if (entry != NULL) {
          /* Retry once without stream payload so ngtcp2 can emit connection-level frames. */
          force_conn_only = 1;
          continue;
        }
        break;
      }
      if (nwrite == NGTCP2_ERR_STREAM_SHUT_WR) {
        /* Peer already shut write-side for this stream; drop buffered tx for it. */
        php_quic_connection_discard_tx_entry(entry);
        continue;
      }
      if (php_quic_connection_is_nonfatal_closing_error((int)nwrite)) {
        php_quic_connection_sync_state(connection);
        break;
      }

      ngtcp2_ccerr_set_liberr(&connection->last_error, (int)nwrite, NULL, 0);
      php_quic_connection_throw_ngtcp2_error((int)nwrite,
                                             "ngtcp2_conn_writev_stream");
      RETURN_THROWS();
    }

    php_quic_connection_update_tx_entry(connection, entry, wdatalen, fin != 0,
                                        nwrite > 0);

    if (nwrite == 0) {
      break;
    }

    object_init_ex(&datagram_zv, php_quic_datagram_ce);
    datagram = Z_QUIC_DATAGRAM_P(&datagram_zv);

    if (datagram->payload != NULL) {
      zend_string_release(datagram->payload);
    }
    datagram->payload = zend_string_init((const char *)buf, (size_t)nwrite, 0);

    if (!Z_ISUNDEF(datagram->peer_address)) {
      zval_ptr_dtor(&datagram->peer_address);
    }
    ZVAL_COPY(&datagram->peer_address, &connection->remote_address_zv);

    if (!Z_ISUNDEF(datagram->local_address)) {
      zval_ptr_dtor(&datagram->local_address);
    }
    ZVAL_COPY(&datagram->local_address, &connection->local_address_zv);

    add_next_index_zval(return_value, &datagram_zv);

    emitted++;
    if (emitted >= 256) {
      break;
    }
  }

  php_quic_connection_sync_state(connection);
}

PHP_METHOD(Ngtcp2_Connection, openStream) {
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  int64_t stream_id;
  int rv;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->conn == NULL || connection->closed) {
    zend_throw_exception(zend_ce_exception, "Connection is closed", 0);
    RETURN_THROWS();
  }

  rv = ngtcp2_conn_open_bidi_stream(connection->conn, &stream_id, NULL);
  if (rv != 0 && rv != NGTCP2_ERR_STREAM_ID_BLOCKED) {
    php_quic_connection_throw_ngtcp2_error(rv, "ngtcp2_conn_open_bidi_stream");
    RETURN_THROWS();
  }

  php_quic_connection_ensure_next_stream_id(connection, 1);

  if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) {
    stream_id = connection->next_bidi_stream_id;
    connection->next_bidi_stream_id += 4;
  } else if (stream_id >= connection->next_bidi_stream_id) {
    connection->next_bidi_stream_id = stream_id + 4;
  }

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->pending_open = rv == NGTCP2_ERR_STREAM_ID_BLOCKED;
  entry->writable = 1;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_OPENED,
                                 stream_id, 0, 0, NULL);
  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE,
                                 stream_id, 0, 0, NULL);

  php_quic_stream_create(return_value, ZEND_THIS, stream_id);
}

PHP_METHOD(Ngtcp2_Connection, openUniStream) {
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  int64_t stream_id;
  int rv;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->conn == NULL || connection->closed) {
    zend_throw_exception(zend_ce_exception, "Connection is closed", 0);
    RETURN_THROWS();
  }

  rv = ngtcp2_conn_open_uni_stream(connection->conn, &stream_id, NULL);
  if (rv != 0 && rv != NGTCP2_ERR_STREAM_ID_BLOCKED) {
    php_quic_connection_throw_ngtcp2_error(rv, "ngtcp2_conn_open_uni_stream");
    RETURN_THROWS();
  }

  php_quic_connection_ensure_next_stream_id(connection, 0);

  if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) {
    stream_id = connection->next_uni_stream_id;
    connection->next_uni_stream_id += 4;
  } else if (stream_id >= connection->next_uni_stream_id) {
    connection->next_uni_stream_id = stream_id + 4;
  }

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->pending_open = rv == NGTCP2_ERR_STREAM_ID_BLOCKED;
  entry->writable = 1;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_OPENED,
                                 stream_id, 0, 0, NULL);
  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE,
                                 stream_id, 0, 0, NULL);

  php_quic_stream_create(return_value, ZEND_THIS, stream_id);
}

PHP_METHOD(Ngtcp2_Connection, getStream) {
  php_quic_connection *connection;
  php_quic_stream_entry *entry;
  zend_long stream_id;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_LONG(stream_id)
  ZEND_PARSE_PARAMETERS_END();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  entry = php_quic_connection_get_stream_entry(connection, (int64_t)stream_id);
  if (entry == NULL) {
    RETURN_NULL();
  }

  php_quic_stream_create(return_value, ZEND_THIS, (int64_t)stream_id);
}

PHP_METHOD(Ngtcp2_Connection, close) {
  php_quic_connection *connection;
  zend_long error_code = 0;
  zend_string *reason = NULL;
  const uint8_t *reason_data = NULL;
  size_t reason_len = 0;

  ZEND_PARSE_PARAMETERS_START(0, 2)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(error_code)
    Z_PARAM_STR(reason)
  ZEND_PARSE_PARAMETERS_END();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->close_requested) {
    return;
  }

  if (connection->close_reason != NULL) {
    zend_string_release(connection->close_reason);
    connection->close_reason = NULL;
  }

  if (reason != NULL && ZSTR_LEN(reason) > 0) {
    connection->close_reason = zend_string_copy(reason);
    reason_data = (const uint8_t *)ZSTR_VAL(connection->close_reason);
    reason_len = ZSTR_LEN(connection->close_reason);
  }

  ngtcp2_ccerr_set_application_error(&connection->last_error,
                                     (uint64_t)error_code, reason_data,
                                     reason_len);
  connection->close_requested = 1;
  connection->closed = 1;

  if (!connection->close_event_emitted) {
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_CONNECTION_CLOSED,
                                   -1, (uint64_t)error_code, 0,
                                   reason != NULL ? ZSTR_VAL(reason) : NULL);
    connection->close_event_emitted = 1;
  }
}

PHP_METHOD(Ngtcp2_Connection, isEstablished) {
  php_quic_connection *connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_BOOL(connection->established);
}

PHP_METHOD(Ngtcp2_Connection, isClosed) {
  php_quic_connection *connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  ZEND_PARSE_PARAMETERS_NONE();
  RETURN_BOOL(connection->closed || connection->close_requested);
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
  PHP_ME(Ngtcp2_Connection, handleTimers, arginfo_connection_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, tick, arginfo_connection_no_args, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, getNextTimeout, arginfo_connection_get_next_timeout,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, getNextExpiry, arginfo_connection_get_next_expiry,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, getTimeoutAt, arginfo_connection_get_next_expiry,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, drainEvents, arginfo_connection_drain_events, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, drainOutgoingDatagrams,
         arginfo_connection_drain_outgoing_datagrams, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, openStream, arginfo_connection_open_stream, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, openUniStream, arginfo_connection_open_uni_stream,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Connection, getStream, arginfo_connection_get_stream, ZEND_ACC_PUBLIC)
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
  memset(&connection->conn_ref, 0, sizeof(connection->conn_ref));
  connection->cred = NULL;
  connection->session = NULL;
  memset(&connection->remote_addr, 0, sizeof(connection->remote_addr));
  memset(&connection->local_addr, 0, sizeof(connection->local_addr));
  connection->remote_addrlen = 0;
  connection->local_addrlen = 0;
  ZVAL_UNDEF(&connection->remote_address_zv);
  ZVAL_UNDEF(&connection->local_address_zv);
  connection->close_reason = NULL;
  connection->next_bidi_stream_id = 0;
  connection->next_uni_stream_id = 2;
  connection->established = 0;
  connection->draining = 0;
  connection->closed = 0;
  connection->close_requested = 0;
  connection->close_packet_sent = 0;
  connection->close_event_emitted = 0;
  connection->draining_event_emitted = 0;

  ngtcp2_ccerr_default(&connection->last_error);
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

  if (!Z_ISUNDEF(connection->remote_address_zv)) {
    zval_ptr_dtor(&connection->remote_address_zv);
    ZVAL_UNDEF(&connection->remote_address_zv);
  }

  if (!Z_ISUNDEF(connection->local_address_zv)) {
    zval_ptr_dtor(&connection->local_address_zv);
    ZVAL_UNDEF(&connection->local_address_zv);
  }

  if (connection->close_reason != NULL) {
    zend_string_release(connection->close_reason);
    connection->close_reason = NULL;
  }

  php_quic_tls_gnutls_cleanup(connection);
  php_quic_event_queue_destroy(&connection->events);
  zend_hash_destroy(&connection->streams);
  zend_object_std_dtor(&connection->std);
}

int php_ngtcp2_connection_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "ClientConfig",
                      php_quic_client_config_methods);
  php_quic_client_config_ce = zend_register_internal_class(&ce);
  php_quic_client_config_ce->create_object = php_quic_client_config_create_object;

  memcpy(&php_quic_client_config_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_client_config_handlers.offset = XtOffsetOf(php_quic_client_config, std);
  php_quic_client_config_handlers.free_obj = php_quic_client_config_free_object;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Connection", php_quic_connection_methods);
  php_quic_connection_ce = zend_register_internal_class(&ce);
  php_quic_connection_ce->create_object = php_quic_connection_create_object;

  memcpy(&php_quic_connection_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_connection_handlers.offset = XtOffsetOf(php_quic_connection, std);
  php_quic_connection_handlers.free_obj = php_quic_connection_free_object;

  return SUCCESS;
}
