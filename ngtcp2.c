#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_ngtcp2.h"

#include <gnutls/gnutls.h>
#include <ext/standard/info.h>

#include "src/internal/address.h"
#include "src/internal/connection.h"
#include "src/internal/datagram.h"
#include "src/internal/event.h"
#include "src/internal/stream.h"

PHP_MINIT_FUNCTION(ngtcp2);
PHP_MSHUTDOWN_FUNCTION(ngtcp2);
PHP_MINFO_FUNCTION(ngtcp2);

zend_module_entry ngtcp2_module_entry = {
  STANDARD_MODULE_HEADER,
  "ngtcp2",
  NULL,
  PHP_MINIT(ngtcp2),
  PHP_MSHUTDOWN(ngtcp2),
  NULL,
  NULL,
  PHP_MINFO(ngtcp2),
  PHP_NGTCP2_VERSION,
  STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_NGTCP2
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(ngtcp2)
#endif

PHP_MINIT_FUNCTION(ngtcp2) {
  int rv;

  rv = gnutls_global_init();
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_global_init failed: %s",
                     gnutls_strerror(rv));
    return FAILURE;
  }

  if (php_ngtcp2_address_init(INIT_FUNC_ARGS_PASSTHRU) != SUCCESS) {
    return FAILURE;
  }

  if (php_ngtcp2_datagram_init(INIT_FUNC_ARGS_PASSTHRU) != SUCCESS) {
    return FAILURE;
  }

  if (php_ngtcp2_event_init(INIT_FUNC_ARGS_PASSTHRU) != SUCCESS) {
    return FAILURE;
  }

  if (php_ngtcp2_connection_init(INIT_FUNC_ARGS_PASSTHRU) != SUCCESS) {
    return FAILURE;
  }

  if (php_ngtcp2_stream_init(INIT_FUNC_ARGS_PASSTHRU) != SUCCESS) {
    return FAILURE;
  }

  return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(ngtcp2) {
  gnutls_global_deinit();
  return SUCCESS;
}

PHP_MINFO_FUNCTION(ngtcp2) {
  php_info_print_table_start();
  php_info_print_table_row(2, "ngtcp2 support", "enabled");
  php_info_print_table_row(2, "Version", PHP_NGTCP2_VERSION);
  php_info_print_table_end();
}
