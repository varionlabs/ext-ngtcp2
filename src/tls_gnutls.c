#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ngtcp2/ngtcp2_crypto_gnutls.h>

#include "internal/tls.h"

int php_quic_tls_gnutls_init_client(php_quic_connection *connection) {
  int rv;

  rv = gnutls_certificate_allocate_credentials(&connection->cred);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING,
                     "gnutls_certificate_allocate_credentials failed: %s",
                     gnutls_strerror(rv));
    return FAILURE;
  }

  rv = gnutls_init(&connection->session,
                   GNUTLS_CLIENT | GNUTLS_ENABLE_EARLY_DATA |
                     GNUTLS_NO_END_OF_EARLY_DATA);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_init failed: %s",
                     gnutls_strerror(rv));
    gnutls_certificate_free_credentials(connection->cred);
    connection->cred = NULL;
    return FAILURE;
  }

  if (ngtcp2_crypto_gnutls_configure_client_session(connection->session) != 0) {
    php_error_docref(NULL, E_WARNING,
                     "ngtcp2_crypto_gnutls_configure_client_session failed");
    gnutls_deinit(connection->session);
    connection->session = NULL;
    gnutls_certificate_free_credentials(connection->cred);
    connection->cred = NULL;
    return FAILURE;
  }

  rv = gnutls_credentials_set(connection->session, GNUTLS_CRD_CERTIFICATE,
                              connection->cred);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_credentials_set failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  return SUCCESS;
}

void php_quic_tls_gnutls_cleanup(php_quic_connection *connection) {
  if (connection->session != NULL) {
    gnutls_deinit(connection->session);
    connection->session = NULL;
  }

  if (connection->cred != NULL) {
    gnutls_certificate_free_credentials(connection->cred);
    connection->cred = NULL;
  }
}
