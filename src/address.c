#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "internal/address.h"
#include "internal/macros.h"

zend_class_entry *php_quic_address_ce;
static zend_object_handlers php_quic_address_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_address_construct, 0, 0, 2)
  ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
  ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_address_get_host, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_address_get_port, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_Address, __construct) {
  php_quic_address *address;
  zend_string *host;
  zend_long port;

  ZEND_PARSE_PARAMETERS_START(2, 2)
    Z_PARAM_STR(host)
    Z_PARAM_LONG(port)
  ZEND_PARSE_PARAMETERS_END();

  address = Z_QUIC_ADDRESS_P(ZEND_THIS);
  if (address->host != NULL) {
    zend_string_release(address->host);
  }

  address->host = zend_string_copy(host);
  address->port = port;
}

PHP_METHOD(Ngtcp2_Address, getHost) {
  php_quic_address *address = Z_QUIC_ADDRESS_P(ZEND_THIS);
  RETURN_STR_COPY(address->host);
}

PHP_METHOD(Ngtcp2_Address, getPort) {
  php_quic_address *address = Z_QUIC_ADDRESS_P(ZEND_THIS);
  RETURN_LONG(address->port);
}

static const zend_function_entry php_quic_address_methods[] = {
  PHP_ME(Ngtcp2_Address, __construct, arginfo_address_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Address, getHost, arginfo_address_get_host, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Address, getPort, arginfo_address_get_port, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_address_create_object(zend_class_entry *ce) {
  php_quic_address *address;

  address = zend_object_alloc(sizeof(*address), ce);
  address->host = zend_string_init("", 0, 0);
  address->port = 0;

  zend_object_std_init(&address->std, ce);
  object_properties_init(&address->std, ce);
  address->std.handlers = &php_quic_address_handlers;

  return &address->std;
}

static void php_quic_address_free_object(zend_object *object) {
  php_quic_address *address;

  address = (php_quic_address *)((char *)object -
                                 XtOffsetOf(php_quic_address, std));

  if (address->host != NULL) {
    zend_string_release(address->host);
    address->host = NULL;
  }

  zend_object_std_dtor(&address->std);
}

int php_ngtcp2_address_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Address", php_quic_address_methods);
  php_quic_address_ce = zend_register_internal_class(&ce);
  php_quic_address_ce->create_object = php_quic_address_create_object;

  memcpy(&php_quic_address_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_address_handlers.offset = XtOffsetOf(php_quic_address, std);
  php_quic_address_handlers.free_obj = php_quic_address_free_object;

  return SUCCESS;
}
