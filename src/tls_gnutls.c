#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <string.h>

#include "internal/tls.h"

int php_quic_tls_gnutls_init_client(php_quic_connection *connection,
                                    const char *alpn) {
  static const char priority[] =
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:"
    "+CHACHA20-POLY1305:+AES-128-CCM:-GROUP-ALL:+GROUP-SECP256R1:+GROUP-X25519:"
    "+GROUP-SECP384R1:+GROUP-SECP521R1:%DISABLE_TLS13_COMPAT_MODE";
  gnutls_datum_t alpn_proto;
  int rv;

  if (alpn == NULL || alpn[0] == '\0') {
    alpn = "h3";
  }

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

  rv = gnutls_priority_set_direct(connection->session, priority, NULL);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_priority_set_direct failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
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

  rv = gnutls_certificate_set_x509_system_trust(connection->cred);
  if (rv < 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_certificate_set_x509_system_trust: %s",
                     gnutls_strerror(rv));
  }

  rv = gnutls_credentials_set(connection->session, GNUTLS_CRD_CERTIFICATE,
                              connection->cred);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_credentials_set failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  alpn_proto.data = (unsigned char *)alpn;
  alpn_proto.size = strlen(alpn);
  rv = gnutls_alpn_set_protocols(connection->session, &alpn_proto, 1,
                                 GNUTLS_ALPN_MANDATORY);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_alpn_set_protocols failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  gnutls_session_set_ptr(connection->session, &connection->conn_ref);

  return SUCCESS;
}

int php_quic_tls_gnutls_init_server(php_quic_connection *connection,
                                    const char *cert_file,
                                    const char *key_file,
                                    const char *alpn) {
  static const char priority[] =
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:"
    "+CHACHA20-POLY1305:+AES-128-CCM:-GROUP-ALL:+GROUP-SECP256R1:+GROUP-X25519:"
    "+GROUP-SECP384R1:+GROUP-SECP521R1:%DISABLE_TLS13_COMPAT_MODE";
  gnutls_datum_t alpn_proto;
  int rv;

  if (cert_file == NULL || key_file == NULL || alpn == NULL || alpn[0] == '\0') {
    php_error_docref(NULL, E_WARNING,
                     "server TLS initialization requires cert_file, key_file, and alpn");
    return FAILURE;
  }

  rv = gnutls_certificate_allocate_credentials(&connection->cred);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING,
                     "gnutls_certificate_allocate_credentials failed: %s",
                     gnutls_strerror(rv));
    return FAILURE;
  }

  rv = gnutls_certificate_set_x509_key_file(connection->cred, cert_file, key_file,
                                            GNUTLS_X509_FMT_PEM);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_certificate_set_x509_key_file failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  rv = gnutls_init(&connection->session,
                   GNUTLS_SERVER | GNUTLS_ENABLE_EARLY_DATA |
                     GNUTLS_NO_AUTO_SEND_TICKET | GNUTLS_NO_END_OF_EARLY_DATA);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_init failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  rv = gnutls_priority_set_direct(connection->session, priority, NULL);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_priority_set_direct failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  if (ngtcp2_crypto_gnutls_configure_server_session(connection->session) != 0) {
    php_error_docref(NULL, E_WARNING,
                     "ngtcp2_crypto_gnutls_configure_server_session failed");
    php_quic_tls_gnutls_cleanup(connection);
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

  alpn_proto.data = (unsigned char *)alpn;
  alpn_proto.size = strlen(alpn);
  rv = gnutls_alpn_set_protocols(connection->session, &alpn_proto, 1,
                                 GNUTLS_ALPN_MANDATORY |
                                   GNUTLS_ALPN_SERVER_PRECEDENCE);
  if (rv != 0) {
    php_error_docref(NULL, E_WARNING, "gnutls_alpn_set_protocols failed: %s",
                     gnutls_strerror(rv));
    php_quic_tls_gnutls_cleanup(connection);
    return FAILURE;
  }

  gnutls_session_set_ptr(connection->session, &connection->conn_ref);
  gnutls_handshake_set_timeout(connection->session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

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
