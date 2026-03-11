#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_hrtime.h>
#include <string.h>

#include <gnutls/crypto.h>
#include <ngtcp2/ngtcp2.h>

#include "internal/address.h"
#include "internal/callbacks.h"
#include "internal/connection.h"
#include "internal/datagram.h"
#include "internal/macros.h"
#include "internal/server_connection.h"
#include "internal/tls.h"

zend_class_entry *php_quic_server_connection_ce;

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

static zend_string *php_quic_get_required_string_option(HashTable *options,
                                                        const char *name) {
  zval *zv;

  zv = zend_hash_str_find(options, name, strlen(name));
  if (zv == NULL || Z_TYPE_P(zv) != IS_STRING || Z_STRLEN_P(zv) == 0) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ServerConnection::accept options['%s'] must be a non-empty string",
                            name);
    return NULL;
  }

  return zend_string_copy(Z_STR_P(zv));
}

static zend_string *php_quic_get_optional_string_option(HashTable *options,
                                                        const char *name,
                                                        const char *fallback) {
  zval *zv;

  zv = zend_hash_str_find(options, name, strlen(name));
  if (zv == NULL) {
    return zend_string_init(fallback, strlen(fallback), 0);
  }

  if (Z_TYPE_P(zv) != IS_STRING || Z_STRLEN_P(zv) == 0) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ServerConnection::accept options['%s'] must be a non-empty string",
                            name);
    return NULL;
  }

  return zend_string_copy(Z_STR_P(zv));
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_connection_construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_connection_accept, 0, 1,
                                       Varion\\Ngtcp2\\ServerConnection, 0)
  ZEND_ARG_OBJ_INFO(0, initial, Varion\\Ngtcp2\\Datagram, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
  ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_ServerConnection, __construct) {
  zend_throw_exception(
    zend_ce_exception,
    "Varion\\Ngtcp2\\ServerConnection cannot be constructed directly; use ServerConnection::accept()",
    0);
}

PHP_METHOD(Ngtcp2_ServerConnection, accept) {
  php_quic_connection *connection;
  php_quic_datagram *initial_datagram;
  zval *initial;
  zval *local_address = NULL;
  zval *options = NULL;
  zval server_zv;
  zend_string *cert_file = NULL;
  zend_string *key_file = NULL;
  zend_string *alpn = NULL;
  ngtcp2_callbacks callbacks;
  ngtcp2_settings settings;
  ngtcp2_transport_params params;
  ngtcp2_path path;
  ngtcp2_cid dcid;
  ngtcp2_cid scid;
  ngtcp2_pkt_info pi = {0};
  ngtcp2_version_cid vc;
  struct sockaddr_storage remote_addr;
  struct sockaddr_storage local_addr;
  socklen_t remote_addrlen;
  socklen_t local_addrlen;
  int rv;

  ZEND_PARSE_PARAMETERS_START(1, 3)
    Z_PARAM_OBJECT_OF_CLASS(initial, php_quic_datagram_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_ARRAY_OR_NULL(options)
  ZEND_PARSE_PARAMETERS_END();

  initial_datagram = Z_QUIC_DATAGRAM_P(initial);
  if (initial_datagram->payload == NULL ||
      ZSTR_LEN(initial_datagram->payload) == 0) {
    zend_throw_exception(zend_ce_exception, "initial datagram payload is empty",
                         0);
    RETURN_THROWS();
  }

  memset(&vc, 0, sizeof(vc));
  rv = ngtcp2_pkt_decode_version_cid(
    &vc, (const uint8_t *)ZSTR_VAL(initial_datagram->payload),
    ZSTR_LEN(initial_datagram->payload), 0);
  if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "failed to decode initial datagram: %s",
                            ngtcp2_strerror(rv));
    RETURN_THROWS();
  }

  if (!ngtcp2_is_supported_version(vc.version)) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "unsupported QUIC version in initial datagram: 0x%08x",
                            vc.version);
    RETURN_THROWS();
  }

  if (vc.dcidlen > NGTCP2_MAX_CIDLEN || vc.scidlen > NGTCP2_MAX_CIDLEN) {
    zend_throw_exception(zend_ce_exception,
                         "initial datagram has unsupported CID length", 0);
    RETURN_THROWS();
  }

  if (options == NULL || Z_TYPE_P(options) != IS_ARRAY) {
    zend_throw_exception(
      zend_ce_exception,
      "ServerConnection::accept requires options array with certFile and keyFile",
      0);
    RETURN_THROWS();
  }

  cert_file = php_quic_get_required_string_option(Z_ARRVAL_P(options), "certFile");
  if (cert_file == NULL) {
    RETURN_THROWS();
  }

  key_file = php_quic_get_required_string_option(Z_ARRVAL_P(options), "keyFile");
  if (key_file == NULL) {
    zend_string_release(cert_file);
    RETURN_THROWS();
  }

  alpn = php_quic_get_optional_string_option(Z_ARRVAL_P(options), "alpn", "hq-interop");
  if (alpn == NULL) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    RETURN_THROWS();
  }

  if (php_quic_address_to_sockaddr(&initial_datagram->remote_address, &remote_addr,
                                   &remote_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "Datagram.remoteAddress is invalid", 0);
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    RETURN_THROWS();
  }

  if (local_address != NULL) {
    if (php_quic_address_to_sockaddr(local_address, &local_addr,
                                     &local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "localAddress is invalid", 0);
      zend_string_release(cert_file);
      zend_string_release(key_file);
      zend_string_release(alpn);
      RETURN_THROWS();
    }
  } else if (!Z_ISUNDEF(initial_datagram->local_address) &&
             Z_TYPE(initial_datagram->local_address) == IS_OBJECT) {
    if (php_quic_address_to_sockaddr(&initial_datagram->local_address,
                                     &local_addr, &local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "Datagram.localAddress is invalid", 0);
      zend_string_release(cert_file);
      zend_string_release(key_file);
      zend_string_release(alpn);
      RETURN_THROWS();
    }
  } else if (php_quic_default_local_addr(remote_addr.ss_family, &local_addr,
                                         &local_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "failed to initialize local address", 0);
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    RETURN_THROWS();
  }

  object_init_ex(&server_zv, php_quic_server_connection_ce);
  connection = Z_QUIC_CONNECTION_P(&server_zv);

  ZVAL_COPY(&connection->remote_address_zv, &initial_datagram->remote_address);
  if (local_address != NULL) {
    ZVAL_COPY(&connection->local_address_zv, local_address);
  } else if (!Z_ISUNDEF(initial_datagram->local_address) &&
             Z_TYPE(initial_datagram->local_address) == IS_OBJECT) {
    ZVAL_COPY(&connection->local_address_zv, &initial_datagram->local_address);
  } else {
    php_quic_address_init_from_sockaddr(&connection->local_address_zv,
                                        (struct sockaddr *)&local_addr,
                                        local_addrlen);
  }

  connection->remote_addr = remote_addr;
  connection->remote_addrlen = remote_addrlen;
  connection->local_addr = local_addr;
  connection->local_addrlen = local_addrlen;

  connection->conn_ref.get_conn = php_quic_get_conn;
  connection->conn_ref.user_data = connection;

  if (php_quic_tls_gnutls_init_server(connection, ZSTR_VAL(cert_file),
                                      ZSTR_VAL(key_file), ZSTR_VAL(alpn)) != SUCCESS) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception(zend_ce_exception,
                         "failed to initialize server-side GnuTLS session", 0);
    RETURN_THROWS();
  }

  php_quic_callbacks_init(&callbacks);

  dcid.datalen = vc.scidlen;
  memcpy(dcid.data, vc.scid, vc.scidlen);

  scid.datalen = 18;
  if (php_quic_fill_random(scid.data, scid.datalen) != SUCCESS) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception(zend_ce_exception, "failed to generate server SCID", 0);
    RETURN_THROWS();
  }

  ngtcp2_settings_default(&settings);
  settings.initial_ts = php_quic_timestamp();

  ngtcp2_transport_params_default(&params);
  params.initial_max_stream_data_bidi_local = 64 * 1024;
  params.initial_max_stream_data_bidi_remote = 64 * 1024;
  params.initial_max_stream_data_uni = 64 * 1024;
  params.initial_max_data = 1024 * 1024;
  params.initial_max_streams_bidi = 16;
  params.initial_max_streams_uni = 16;
  params.max_idle_timeout = 30 * NGTCP2_SECONDS;
  params.active_connection_id_limit = 7;
  params.stateless_reset_token_present = 1;
  if (php_quic_fill_random(params.stateless_reset_token,
                           sizeof(params.stateless_reset_token)) != SUCCESS) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception(zend_ce_exception,
                         "failed to generate stateless reset token", 0);
    RETURN_THROWS();
  }
  ngtcp2_cid_init(&params.original_dcid, vc.dcid, vc.dcidlen);
  params.original_dcid_present = 1;

  memset(&path, 0, sizeof(path));
  path.local.addr = (struct sockaddr *)&connection->local_addr;
  path.local.addrlen = connection->local_addrlen;
  path.remote.addr = (struct sockaddr *)&connection->remote_addr;
  path.remote.addrlen = connection->remote_addrlen;

  rv = ngtcp2_conn_server_new(&connection->conn, &dcid, &scid, &path, vc.version,
                              &callbacks, &settings, &params, NULL, connection);
  if (rv != 0) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ngtcp2_conn_server_new failed: %s", ngtcp2_strerror(rv));
    RETURN_THROWS();
  }

  ngtcp2_conn_set_tls_native_handle(connection->conn, connection->session);

  rv = ngtcp2_conn_read_pkt(connection->conn, &path, &pi,
                            (const uint8_t *)ZSTR_VAL(initial_datagram->payload),
                            ZSTR_LEN(initial_datagram->payload), php_quic_timestamp());
  if (rv != 0) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ngtcp2_conn_read_pkt (initial) failed: %s",
                            ngtcp2_strerror(rv));
    RETURN_THROWS();
  }

  zend_string_release(cert_file);
  zend_string_release(key_file);
  zend_string_release(alpn);

  RETURN_ZVAL(&server_zv, 0, 1);
}

static const zend_function_entry php_quic_server_connection_methods[] = {
  PHP_ME(Ngtcp2_ServerConnection, __construct, arginfo_server_connection_construct,
         ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
  PHP_ME(Ngtcp2_ServerConnection, accept, arginfo_server_connection_accept,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_FE_END
};

int php_ngtcp2_server_connection_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "ServerConnection",
                      php_quic_server_connection_methods);
  php_quic_server_connection_ce =
    zend_register_internal_class_ex(&ce, php_quic_connection_ce);

  return SUCCESS;
}
