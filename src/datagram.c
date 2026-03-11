#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "internal/address.h"
#include "internal/datagram.h"
#include "internal/macros.h"

zend_class_entry *php_quic_datagram_ce;
static zend_object_handlers php_quic_datagram_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_datagram_construct, 0, 0, 2)
  ZEND_ARG_TYPE_INFO(0, payload, IS_STRING, 0)
  ZEND_ARG_OBJ_INFO(0, remoteAddress, Varion\\Ngtcp2\\Address, 0)
  ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, localAddress, Varion\\Ngtcp2\\Address, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_datagram_get_payload, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_datagram_get_remote, 0, 0, Varion\\Ngtcp2\\Address, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_datagram_get_local, 0, 0, Varion\\Ngtcp2\\Address, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(Ngtcp2_Datagram, __construct) {
  php_quic_datagram *datagram;
  zend_string *payload;
  zval *remote_address;
  zval *local_address = NULL;

  ZEND_PARSE_PARAMETERS_START(2, 3)
    Z_PARAM_STR(payload)
    Z_PARAM_OBJECT_OF_CLASS(remote_address, php_quic_address_ce)
    Z_PARAM_OPTIONAL
    Z_PARAM_OBJECT_OF_CLASS_OR_NULL(local_address, php_quic_address_ce)
  ZEND_PARSE_PARAMETERS_END();

  datagram = Z_QUIC_DATAGRAM_P(ZEND_THIS);

  if (datagram->payload != NULL) {
    zend_string_release(datagram->payload);
  }
  datagram->payload = zend_string_copy(payload);

  if (!Z_ISUNDEF(datagram->remote_address)) {
    zval_ptr_dtor(&datagram->remote_address);
  }
  ZVAL_COPY(&datagram->remote_address, remote_address);

  if (!Z_ISUNDEF(datagram->local_address)) {
    zval_ptr_dtor(&datagram->local_address);
  }
  if (local_address != NULL) {
    ZVAL_COPY(&datagram->local_address, local_address);
  } else {
    ZVAL_NULL(&datagram->local_address);
  }
}

PHP_METHOD(Ngtcp2_Datagram, getPayload) {
  php_quic_datagram *datagram = Z_QUIC_DATAGRAM_P(ZEND_THIS);
  RETURN_STR_COPY(datagram->payload);
}

PHP_METHOD(Ngtcp2_Datagram, getRemoteAddress) {
  php_quic_datagram *datagram = Z_QUIC_DATAGRAM_P(ZEND_THIS);
  RETURN_COPY(&datagram->remote_address);
}

PHP_METHOD(Ngtcp2_Datagram, getLocalAddress) {
  php_quic_datagram *datagram = Z_QUIC_DATAGRAM_P(ZEND_THIS);
  RETURN_COPY(&datagram->local_address);
}

static const zend_function_entry php_quic_datagram_methods[] = {
  PHP_ME(Ngtcp2_Datagram, __construct, arginfo_datagram_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Datagram, getPayload, arginfo_datagram_get_payload, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Datagram, getRemoteAddress, arginfo_datagram_get_remote, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Datagram, getLocalAddress, arginfo_datagram_get_local, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

static zend_object *php_quic_datagram_create_object(zend_class_entry *ce) {
  php_quic_datagram *datagram;

  datagram = zend_object_alloc(sizeof(*datagram), ce);
  datagram->payload = zend_string_init("", 0, 0);
  ZVAL_UNDEF(&datagram->remote_address);
  ZVAL_UNDEF(&datagram->local_address);

  zend_object_std_init(&datagram->std, ce);
  object_properties_init(&datagram->std, ce);
  datagram->std.handlers = &php_quic_datagram_handlers;

  return &datagram->std;
}

static void php_quic_datagram_free_object(zend_object *object) {
  php_quic_datagram *datagram;

  datagram = (php_quic_datagram *)((char *)object -
                                   XtOffsetOf(php_quic_datagram, std));

  if (datagram->payload != NULL) {
    zend_string_release(datagram->payload);
    datagram->payload = NULL;
  }

  if (!Z_ISUNDEF(datagram->remote_address)) {
    zval_ptr_dtor(&datagram->remote_address);
    ZVAL_UNDEF(&datagram->remote_address);
  }

  if (!Z_ISUNDEF(datagram->local_address)) {
    zval_ptr_dtor(&datagram->local_address);
    ZVAL_UNDEF(&datagram->local_address);
  }

  zend_object_std_dtor(&datagram->std);
}

int php_ngtcp2_datagram_init(INIT_FUNC_ARGS) {
  zend_class_entry ce;

  INIT_NS_CLASS_ENTRY(ce, "Varion\\Ngtcp2", "Datagram", php_quic_datagram_methods);
  php_quic_datagram_ce = zend_register_internal_class(&ce);
  php_quic_datagram_ce->create_object = php_quic_datagram_create_object;

  memcpy(&php_quic_datagram_handlers, zend_get_std_object_handlers(),
         sizeof(zend_object_handlers));
  php_quic_datagram_handlers.offset = XtOffsetOf(php_quic_datagram, std);
  php_quic_datagram_handlers.free_obj = php_quic_datagram_free_object;

  return SUCCESS;
}
