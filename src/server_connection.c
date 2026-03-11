#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Zend/zend_exceptions.h>
#include <string.h>

#include <ngtcp2/ngtcp2.h>

#include "internal/address.h"
#include "internal/connection.h"
#include "internal/datagram.h"
#include "internal/macros.h"
#include "internal/server_connection.h"

zend_class_entry *php_quic_server_connection_ce;

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
  php_quic_datagram *initial_datagram;
  zval *initial;
  zval *local_address = NULL;
  zval *options = NULL;
  ngtcp2_version_cid vc;
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

  (void)local_address;
  (void)options;

  zend_throw_exception(zend_ce_exception,
                       "ServerConnection::accept is not implemented yet (native server init pending)",
                       0);
  RETURN_THROWS();
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
