#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <Zend/zend_exceptions.h>
#include <ext/spl/spl_exceptions.h>

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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_address_from_string, 0, 1,
                                       Varion\\Ngtcp2\\Address, 0)
  ZEND_ARG_TYPE_INFO(0, endpoint, IS_STRING, 0)
ZEND_END_ARG_INFO()

static int php_quic_address_parse_endpoint(zend_string *endpoint, zend_string **host_out,
                                           zend_long *port_out) {
  const char *value = ZSTR_VAL(endpoint);
  size_t len = ZSTR_LEN(endpoint);
  const char *host_start = NULL;
  size_t host_len = 0;
  const char *port_start = NULL;
  char *endptr = NULL;
  long parsed_port;

  if (len == 0) {
    return FAILURE;
  }

  if (value[0] == '[') {
    const char *closing = strchr(value + 1, ']');
    if (closing == NULL) {
      return FAILURE;
    }

    host_start = value + 1;
    host_len = (size_t)(closing - host_start);
    if (host_len == 0) {
      return FAILURE;
    }

    if ((size_t)(closing - value + 1) >= len || closing[1] != ':') {
      return FAILURE;
    }

    port_start = closing + 2;
  } else {
    const char *sep = strrchr(value, ':');
    const char *first = strchr(value, ':');
    if (sep == NULL || sep == value) {
      return FAILURE;
    }
    if (first != sep) {
      return FAILURE;
    }

    host_start = value;
    host_len = (size_t)(sep - value);
    port_start = sep + 1;
  }

  if (port_start == NULL || *port_start == '\0') {
    return FAILURE;
  }

  parsed_port = strtol(port_start, &endptr, 10);
  if (*port_start == '\0' || endptr == NULL || *endptr != '\0') {
    return FAILURE;
  }
  if (parsed_port < 0 || parsed_port > 65535) {
    return FAILURE;
  }

  *host_out = zend_string_init(host_start, host_len, 0);
  *port_out = (zend_long)parsed_port;
  return SUCCESS;
}

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

PHP_METHOD(Ngtcp2_Address, fromString) {
  zend_string *endpoint;
  zend_string *host = NULL;
  zend_long port = 0;
  php_quic_address *address;

  ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STR(endpoint)
  ZEND_PARSE_PARAMETERS_END();

  if (php_quic_address_parse_endpoint(endpoint, &host, &port) != SUCCESS) {
    zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0,
                            "Address::fromString [endpoint]: invalid endpoint format, expected host:port or [ipv6]:port");
    RETURN_THROWS();
  }

  object_init_ex(return_value, php_quic_address_ce);
  address = Z_QUIC_ADDRESS_P(return_value);
  if (address->host != NULL) {
    zend_string_release(address->host);
  }
  address->host = host;
  address->port = port;
}

static const zend_function_entry php_quic_address_methods[] = {
  PHP_ME(Ngtcp2_Address, __construct, arginfo_address_construct, ZEND_ACC_PUBLIC)
  PHP_ME(Ngtcp2_Address, fromString, arginfo_address_from_string,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
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

int php_quic_address_to_sockaddr(const zval *address_zv,
                                 struct sockaddr_storage *storage,
                                 socklen_t *addrlen) {
  php_quic_address *address;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *rp;
  char port_buf[8];
  int rv;

  if (Z_TYPE_P(address_zv) != IS_OBJECT ||
      !instanceof_function(Z_OBJCE_P(address_zv), php_quic_address_ce)) {
    php_error_docref(NULL, E_WARNING, "address must be Varion\\Ngtcp2\\Address");
    return FAILURE;
  }

  address = Z_QUIC_ADDRESS_P(address_zv);
  if (address->host == NULL || ZSTR_LEN(address->host) == 0) {
    php_error_docref(NULL, E_WARNING, "address host must not be empty");
    return FAILURE;
  }

  if (address->port < 0 || address->port > 65535) {
    php_error_docref(NULL, E_WARNING, "address port must be in range 0..65535");
    return FAILURE;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICSERV;

  snprintf(port_buf, sizeof(port_buf), "%ld", address->port);
  rv = getaddrinfo(ZSTR_VAL(address->host), port_buf, &hints, &res);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "getaddrinfo failed for %s:%s: %s",
                     ZSTR_VAL(address->host), port_buf, gai_strerror(rv));
    return FAILURE;
  }

  for (rp = res; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET || rp->ai_family == AF_INET6) {
      memset(storage, 0, sizeof(*storage));
      memcpy(storage, rp->ai_addr, rp->ai_addrlen);
      *addrlen = (socklen_t)rp->ai_addrlen;
      freeaddrinfo(res);
      return SUCCESS;
    }
  }

  freeaddrinfo(res);
  php_error_docref(NULL, E_WARNING, "unsupported address family for host %s",
                   ZSTR_VAL(address->host));
  return FAILURE;
}

void php_quic_address_init_from_sockaddr(zval *return_value,
                                         const struct sockaddr *addr,
                                         socklen_t addrlen) {
  php_quic_address *address;
  char host[INET6_ADDRSTRLEN];
  uint16_t port = 0;

  (void)addrlen;
  host[0] = '\0';

  if (addr != NULL) {
    switch (addr->sa_family) {
    case AF_INET: {
      const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
      inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host));
      port = ntohs(sin->sin_port);
      break;
    }
    case AF_INET6: {
      const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
      inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof(host));
      port = ntohs(sin6->sin6_port);
      break;
    }
    default:
      break;
    }
  }

  object_init_ex(return_value, php_quic_address_ce);
  address = Z_QUIC_ADDRESS_P(return_value);

  if (address->host != NULL) {
    zend_string_release(address->host);
  }
  address->host = zend_string_init(host, strlen(host), 0);
  address->port = (zend_long)port;
}
