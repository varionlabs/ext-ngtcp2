#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_hrtime.h>
#include <stdlib.h>
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
static zend_class_entry *php_quic_server_config_ce;
static zend_object_handlers php_quic_server_config_handlers;

static ngtcp2_tstamp php_quic_timestamp(void) {
  return (ngtcp2_tstamp)zend_hrtime();
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

static int php_quic_server_set_stateless_reset_token(
  ngtcp2_transport_params *params) {
  params->stateless_reset_token_present = 1;

  if (php_quic_fill_random(params->stateless_reset_token,
                           NGTCP2_STATELESS_RESET_TOKENLEN) != SUCCESS) {
    params->stateless_reset_token_present = 0;
    return FAILURE;
  }

  return SUCCESS;
}

static zend_bool php_quic_test_force_server_new_failure(void) {
  const char *flag = getenv("NGTCP2_TEST_FORCE_SERVER_NEW_FAILURE");

  if (flag == NULL) {
    return 0;
  }

  if (strcmp(flag, "1") == 0 || strcasecmp(flag, "true") == 0 ||
      strcasecmp(flag, "yes") == 0) {
    return 1;
  }

  return 0;
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_connection_construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_server_config_construct, 0, 0, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, certFile, IS_STRING, 1, "null")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, keyFile, IS_STRING, 1, "null")
  ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, alpn, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_config_with_local_address, 0, 1,
                                       Varion\\Ngtcp2\\ServerConfig, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_config_with_certificate, 0, 2,
                                       Varion\\Ngtcp2\\ServerConfig, 0)
  ZEND_ARG_TYPE_INFO(0, certFile, IS_STRING, 0)
  ZEND_ARG_TYPE_INFO(0, keyFile, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_config_with_alpn, 0, 1,
                                       Varion\\Ngtcp2\\ServerConfig, 0)
  ZEND_ARG_TYPE_INFO(0, alpn, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_config_get_local_address, 0, 0,
                                       Varion\\Ngtcp2\\Address, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_config_get_cert_file, 0, 0, IS_STRING,
                                        1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_config_get_key_file, 0, 0, IS_STRING,
                                        1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_server_config_get_alpn, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_connection_accept, 0, 2,
                                       Varion\\Ngtcp2\\ServerConnection, 0)
  ZEND_ARG_OBJ_INFO(0, initial, Varion\\Ngtcp2\\Datagram, 0)
  ZEND_ARG_OBJ_INFO(0, config, Varion\\Ngtcp2\\ServerConfig, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_ServerConfig, __construct) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);
  zval *local_address = NULL;
  zend_string *cert_file = NULL;
  zend_string *key_file = NULL;
  zend_string *alpn = NULL;

  ZEND_PARSE_PARAMETERS_START(0, 4)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_STR_OR_NULL(cert_file)
    Z_PARAM_STR_OR_NULL(key_file)
    Z_PARAM_STR_OR_NULL(alpn)
  ZEND_PARSE_PARAMETERS_END();

  if (cert_file != NULL && ZSTR_LEN(cert_file) == 0) {
    zend_argument_value_error(2, "must be a non-empty string or null");
    RETURN_THROWS();
  }
  if (key_file != NULL && ZSTR_LEN(key_file) == 0) {
    zend_argument_value_error(3, "must be a non-empty string or null");
    RETURN_THROWS();
  }
  if (alpn != NULL && ZSTR_LEN(alpn) == 0) {
    zend_argument_value_error(4, "must be a non-empty string or null");
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

  php_quic_set_nullable_string(&config->cert_file, cert_file);
  php_quic_set_nullable_string(&config->key_file, key_file);
  php_quic_set_nullable_string(&config->alpn, alpn);
}

PHP_METHOD(Ngtcp2_ServerConfig, withLocalAddress) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);
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

PHP_METHOD(Ngtcp2_ServerConfig, withCertificate) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);
  zend_string *cert_file;
  zend_string *key_file;

  ZEND_PARSE_PARAMETERS_START(2, 2)
    Z_PARAM_STR(cert_file)
    Z_PARAM_STR(key_file)
  ZEND_PARSE_PARAMETERS_END();

  if (ZSTR_LEN(cert_file) == 0) {
    zend_argument_value_error(1, "must be a non-empty string");
    RETURN_THROWS();
  }
  if (ZSTR_LEN(key_file) == 0) {
    zend_argument_value_error(2, "must be a non-empty string");
    RETURN_THROWS();
  }

  php_quic_set_nullable_string(&config->cert_file, cert_file);
  php_quic_set_nullable_string(&config->key_file, key_file);
  RETURN_ZVAL(ZEND_THIS, 1, 0);
}

PHP_METHOD(Ngtcp2_ServerConfig, withAlpn) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);
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

PHP_METHOD(Ngtcp2_ServerConfig, getLocalAddress) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);
  RETURN_COPY(&config->local_address);
}

PHP_METHOD(Ngtcp2_ServerConfig, getCertFile) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);

  if (config->cert_file == NULL) {
    RETURN_NULL();
  }
  RETURN_STR_COPY(config->cert_file);
}

PHP_METHOD(Ngtcp2_ServerConfig, getKeyFile) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);

  if (config->key_file == NULL) {
    RETURN_NULL();
  }
  RETURN_STR_COPY(config->key_file);
}

PHP_METHOD(Ngtcp2_ServerConfig, getAlpn) {
  php_quic_server_config *config = Z_QUIC_SERVER_CONFIG_P(ZEND_THIS);

  if (config->alpn == NULL) {
    RETURN_NULL();
  }
  RETURN_STR_COPY(config->alpn);
}

static const zend_function_entry php_quic_server_config_methods[] = {
  PHP_ME(Ngtcp2_ServerConfig, __construct, arginfo_server_config_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, withLocalAddress, arginfo_server_config_with_local_address,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, withCertificate, arginfo_server_config_with_certificate,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, withAlpn, arginfo_server_config_with_alpn, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, getLocalAddress, arginfo_server_config_get_local_address,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, getCertFile, arginfo_server_config_get_cert_file,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, getKeyFile, arginfo_server_config_get_key_file,
         ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_ServerConfig, getAlpn, arginfo_server_config_get_alpn, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_server_config_create_object(zend_class_entry *ce) {
  php_quic_server_config *config;

  config = zend_object_alloc(sizeof(*config), ce);
  ZVAL_UNDEF(&config->local_address);
  ZVAL_NULL(&config->local_address);
  config->cert_file = NULL;
  config->key_file = NULL;
  config->alpn = NULL;

  zend_object_std_init(&config->std, ce);
  object_properties_init(&config->std, ce);
  config->std.handlers = &php_quic_server_config_handlers;

  return &config->std;
}

static void php_quic_server_config_free_object(zend_object *object) {
  php_quic_server_config *config;

  config = (php_quic_server_config *)((char *)object -
                                      XtOffsetOf(php_quic_server_config, std));

  if (!Z_ISUNDEF(config->local_address)) {
    zval_ptr_dtor(&config->local_address);
    ZVAL_UNDEF(&config->local_address);
  }

  php_quic_set_nullable_string(&config->cert_file, NULL);
  php_quic_set_nullable_string(&config->key_file, NULL);
  php_quic_set_nullable_string(&config->alpn, NULL);

  zend_object_std_dtor(&config->std);
}

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
  zval *config_zv;
  zval *effective_local_address = NULL;
  php_quic_server_config *server_config = NULL;
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

  ZEND_PARSE_PARAMETERS_START(2, 2)
    Z_PARAM_OBJECT_OF_CLASS(initial, php_quic_datagram_ce)
    Z_PARAM_OBJECT_OF_CLASS(config_zv, php_quic_server_config_ce)
  ZEND_PARSE_PARAMETERS_END();

  server_config = Z_QUIC_SERVER_CONFIG_P(config_zv);
  if (Z_TYPE(server_config->local_address) == IS_OBJECT) {
    effective_local_address = &server_config->local_address;
  }
  if (server_config->cert_file == NULL || server_config->key_file == NULL) {
    zend_throw_exception(
      zend_ce_exception,
      "ServerConnection::accept [config]: ServerConfig.certFile and ServerConfig.keyFile are required",
      0);
    RETURN_THROWS();
  }
  cert_file = zend_string_copy(server_config->cert_file);
  key_file = zend_string_copy(server_config->key_file);
  if (server_config->alpn != NULL) {
    alpn = zend_string_copy(server_config->alpn);
  } else {
    alpn = zend_string_init("hq-interop", sizeof("hq-interop") - 1, 0);
  }

  initial_datagram = Z_QUIC_DATAGRAM_P(initial);
  if (initial_datagram->payload == NULL ||
      ZSTR_LEN(initial_datagram->payload) == 0) {
    zend_throw_exception(zend_ce_exception, "ServerConnection::accept [initial]: initial datagram payload is empty",
                         0);
    RETURN_THROWS();
  }

  memset(&vc, 0, sizeof(vc));
  rv = ngtcp2_pkt_decode_version_cid(
    &vc, (const uint8_t *)ZSTR_VAL(initial_datagram->payload),
    ZSTR_LEN(initial_datagram->payload), 0);
  if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ServerConnection::accept [decode]: failed to decode initial datagram: %s",
                            ngtcp2_strerror(rv));
    RETURN_THROWS();
  }

  if (!ngtcp2_is_supported_version(vc.version)) {
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ServerConnection::accept [decode]: unsupported QUIC version in initial datagram: 0x%08x",
                            vc.version);
    RETURN_THROWS();
  }

  if (vc.dcidlen > NGTCP2_MAX_CIDLEN || vc.scidlen > NGTCP2_MAX_CIDLEN) {
    zend_throw_exception(zend_ce_exception,
                         "ServerConnection::accept [decode]: initial datagram has unsupported CID length", 0);
    RETURN_THROWS();
  }

  if (php_quic_address_to_sockaddr(&initial_datagram->peer_address, &remote_addr,
                                   &remote_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "ServerConnection::accept [address]: Datagram.peerAddress is invalid", 0);
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    RETURN_THROWS();
  }

  if (effective_local_address != NULL) {
    if (php_quic_address_to_sockaddr(effective_local_address, &local_addr,
                                     &local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "ServerConnection::accept [address]: localAddress is invalid", 0);
      zend_string_release(cert_file);
      zend_string_release(key_file);
      zend_string_release(alpn);
      RETURN_THROWS();
    }
  } else if (!Z_ISUNDEF(initial_datagram->local_address) &&
             Z_TYPE(initial_datagram->local_address) == IS_OBJECT) {
    if (php_quic_address_to_sockaddr(&initial_datagram->local_address,
                                     &local_addr, &local_addrlen) != SUCCESS) {
      zend_throw_exception(zend_ce_exception, "ServerConnection::accept [address]: Datagram.localAddress is invalid", 0);
      zend_string_release(cert_file);
      zend_string_release(key_file);
      zend_string_release(alpn);
      RETURN_THROWS();
    }
  } else if (php_quic_default_local_addr(remote_addr.ss_family, &local_addr,
                                         &local_addrlen) != SUCCESS) {
    zend_throw_exception(zend_ce_exception, "ServerConnection::accept [address]: failed to initialize local address", 0);
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    RETURN_THROWS();
  }

  object_init_ex(&server_zv, php_quic_server_connection_ce);
  connection = Z_QUIC_CONNECTION_P(&server_zv);

  ZVAL_COPY(&connection->remote_address_zv, &initial_datagram->peer_address);
  if (effective_local_address != NULL) {
    ZVAL_COPY(&connection->local_address_zv, effective_local_address);
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
                         "ServerConnection::accept [tls]: failed to initialize server-side GnuTLS session", 0);
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
    zend_throw_exception(zend_ce_exception, "ServerConnection::accept [cid]: failed to generate server SCID", 0);
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
  if (php_quic_server_set_stateless_reset_token(&params) != SUCCESS) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception(zend_ce_exception,
                         "ServerConnection::accept [transport]: failed to generate stateless reset token", 0);
    RETURN_THROWS();
  }
  ngtcp2_cid_init(&params.original_dcid, vc.dcid, vc.dcidlen);
  params.original_dcid_present = 1;

  memset(&path, 0, sizeof(path));
  path.local.addr = (struct sockaddr *)&connection->local_addr;
  path.local.addrlen = connection->local_addrlen;
  path.remote.addr = (struct sockaddr *)&connection->remote_addr;
  path.remote.addrlen = connection->remote_addrlen;

  if (php_quic_test_force_server_new_failure()) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception(
      zend_ce_exception,
      "ServerConnection::accept [ngtcp2:new]: forced failure by NGTCP2_TEST_FORCE_SERVER_NEW_FAILURE",
      0);
    RETURN_THROWS();
  }

  rv = ngtcp2_conn_server_new(&connection->conn, &dcid, &scid, &path, vc.version,
                              &callbacks, &settings, &params, NULL, connection);
  if (rv != 0) {
    zend_string_release(cert_file);
    zend_string_release(key_file);
    zend_string_release(alpn);
    zval_ptr_dtor(&server_zv);
    zend_throw_exception_ex(zend_ce_exception, 0,
                            "ServerConnection::accept [ngtcp2:new]: ngtcp2_conn_server_new failed: %s", ngtcp2_strerror(rv));
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
                            "ServerConnection::accept [ngtcp2:read_initial]: ngtcp2_conn_read_pkt (initial) failed: %s",
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

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "ServerConfig",
                      php_quic_server_config_methods);
  php_quic_server_config_ce = zend_register_internal_class(&ce);
  php_quic_server_config_ce->create_object = php_quic_server_config_create_object;

  memcpy(&php_quic_server_config_handlers, &std_object_handlers,
         sizeof(php_quic_server_config_handlers));
  php_quic_server_config_handlers.offset = XtOffsetOf(php_quic_server_config, std);
  php_quic_server_config_handlers.free_obj = php_quic_server_config_free_object;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "ServerConnection",
                      php_quic_server_connection_methods);
  php_quic_server_connection_ce =
    zend_register_internal_class_ex(&ce, php_quic_connection_ce);

  return SUCCESS;
}
