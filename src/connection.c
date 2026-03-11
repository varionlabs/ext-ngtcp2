#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <inttypes.h>
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
static zend_object_handlers php_quic_connection_handlers;

static ngtcp2_tstamp php_quic_timestamp(void) {
  return (ngtcp2_tstamp)zend_hrtime();
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
  ZEND_ARG_OBJ_INFO(0, remoteAddress, Varion\\Ngtcp2\\Address, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
  ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connection_recv, 0, 0, 1)
  ZEND_ARG_OBJ_INFO(0, datagram, Varion\\Ngtcp2\\Datagram, 0)
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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_connection_open_stream, 0, 0,
                                       Varion\\Ngtcp2\\Stream, 0)
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
  php_quic_address *remote_address_obj;
  const char *sni;
  int rv;

  ZEND_PARSE_PARAMETERS_START(1, 3)
    Z_PARAM_OBJECT_OF_CLASS(remote_address, php_quic_address_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_ARRAY_OR_NULL(options)
  ZEND_PARSE_PARAMETERS_END();

  (void)options;

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

  if (connection->session == NULL && php_quic_tls_gnutls_init_client(connection) != SUCCESS) {
    zend_throw_exception(zend_ce_exception,
                         "Failed to initialize GnuTLS for QUIC connection", 0);
    RETURN_THROWS();
  }

  remote_address_obj = Z_QUIC_ADDRESS_P(&connection->remote_address_zv);
  sni = ZSTR_VAL(remote_address_obj->host);
  if (php_quic_is_numeric_host(sni)) {
    sni = "localhost";
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

  if (php_quic_address_to_sockaddr(&datagram->remote_address, &remote_addr,
                                   &remote_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Datagram.remoteAddress is invalid", 0);
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
  int rv;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->conn == NULL || connection->closed) {
    return;
  }

  rv = ngtcp2_conn_handle_expiry(connection->conn, php_quic_timestamp());
  if (rv != 0) {
    ngtcp2_ccerr_set_liberr(&connection->last_error, rv, NULL, 0);
    php_quic_connection_throw_ngtcp2_error(rv, "ngtcp2_conn_handle_expiry");
    RETURN_THROWS();
  }

  php_quic_connection_sync_state(connection);
}

PHP_METHOD(Ngtcp2_Connection, getNextTimeout) {
  php_quic_connection *connection;
  ngtcp2_tstamp now;
  ngtcp2_tstamp expiry;

  ZEND_PARSE_PARAMETERS_NONE();

  connection = Z_QUIC_CONNECTION_P(ZEND_THIS);
  if (connection->conn == NULL || connection->closed) {
    RETURN_NULL();
  }

  expiry = ngtcp2_conn_get_expiry(connection->conn);
  now = php_quic_timestamp();

  if (expiry <= now) {
    RETURN_LONG(0);
  }

  RETURN_LONG((zend_long)((expiry - now) / NGTCP2_MILLISECONDS));
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

      if (!Z_ISUNDEF(datagram->remote_address)) {
        zval_ptr_dtor(&datagram->remote_address);
      }
      ZVAL_COPY(&datagram->remote_address, &connection->remote_address_zv);

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

    entry = php_quic_connection_pick_tx_stream(connection, &stream_id, &datav,
                                               &datavcnt, &fin);

    if (entry != NULL && entry->pending_open) {
      int64_t opened_stream_id = -1;
      int open_rv;

      open_rv = ngtcp2_conn_open_bidi_stream(connection->conn, &opened_stream_id, NULL);
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
        php_quic_connection_throw_ngtcp2_error(open_rv, "ngtcp2_conn_open_bidi_stream");
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

    if (!Z_ISUNDEF(datagram->remote_address)) {
      zval_ptr_dtor(&datagram->remote_address);
    }
    ZVAL_COPY(&datagram->remote_address, &connection->remote_address_zv);

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

  if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) {
    stream_id = connection->next_stream_id;
    connection->next_stream_id += 4;
  } else if (stream_id >= connection->next_stream_id) {
    connection->next_stream_id = stream_id + 4;
  }

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) {
    entry->pending_open = 1;
  }
  entry->writable = 1;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_OPENED,
                                 stream_id, 0, 0, NULL);
  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE,
                                 stream_id, 0, 0, NULL);

  php_quic_stream_create(return_value, ZEND_THIS, stream_id);
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
  connection->next_stream_id = 0;
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

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Connection", php_quic_connection_methods);
  php_quic_connection_ce = zend_register_internal_class(&ce);
  php_quic_connection_ce->create_object = php_quic_connection_create_object;

  memcpy(&php_quic_connection_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_connection_handlers.offset = XtOffsetOf(php_quic_connection, std);
  php_quic_connection_handlers.free_obj = php_quic_connection_free_object;

  return SUCCESS;
}
