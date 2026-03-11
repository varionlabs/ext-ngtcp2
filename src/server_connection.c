#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Zend/zend_exceptions.h>

#include "internal/address.h"
#include "internal/connection.h"
#include "internal/datagram.h"
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
  zval *initial;
  zval *local_address = NULL;
  zval *options = NULL;

  ZEND_PARSE_PARAMETERS_START(1, 3)
    Z_PARAM_OBJECT_OF_CLASS(initial, php_quic_datagram_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
    Z_PARAM_ARRAY_OR_NULL(options)
  ZEND_PARSE_PARAMETERS_END();

  (void)initial;
  (void)local_address;
  (void)options;

  zend_throw_exception(zend_ce_exception,
                       "ServerConnection::accept is not implemented yet", 0);
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
