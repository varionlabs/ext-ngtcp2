#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnutls/crypto.h>
#include <string.h>

#include <ngtcp2/ngtcp2_crypto.h>

#include "internal/buffer.h"
#include "internal/callbacks.h"
#include "internal/connection.h"

static void php_quic_rand_cb(uint8_t *dest, size_t destlen,
                             const ngtcp2_rand_ctx *rand_ctx) {
  (void)rand_ctx;

  if (gnutls_rnd(GNUTLS_RND_NONCE, dest, destlen) != 0) {
    memset(dest, 0, destlen);
  }
}

static int php_quic_get_new_connection_id_cb(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                             uint8_t *token, size_t cidlen,
                                             void *user_data) {
  (void)conn;
  (void)user_data;

  if (gnutls_rnd(GNUTLS_RND_NONCE, cid->data, cidlen) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  if (gnutls_rnd(GNUTLS_RND_NONCE, token, NGTCP2_STATELESS_RESET_TOKENLEN) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  cid->datalen = cidlen;
  return 0;
}

static int php_quic_handshake_completed_cb(ngtcp2_conn *conn, void *user_data) {
  php_quic_connection *connection = user_data;
  (void)conn;

  if (!connection->established) {
    connection->established = 1;
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_HANDSHAKE_COMPLETED, -1, 0, 0,
                                   NULL);
  }

  return 0;
}

static int php_quic_stream_open_cb(ngtcp2_conn *conn, int64_t stream_id,
                                   void *user_data) {
  php_quic_connection *connection = user_data;
  php_quic_stream_entry *entry;
  (void)conn;

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->writable = 1;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_OPENED, stream_id, 0, 1,
                                 NULL);
  return 0;
}

static int php_quic_recv_stream_data_cb(ngtcp2_conn *conn, uint32_t flags,
                                        int64_t stream_id, uint64_t offset,
                                        const uint8_t *data, size_t datalen,
                                        void *user_data, void *stream_user_data) {
  php_quic_connection *connection = user_data;
  php_quic_stream_entry *entry;
  (void)conn;
  (void)offset;
  (void)stream_user_data;

  entry = php_quic_connection_open_stream_entry(connection, stream_id);

  if (datalen > 0) {
    php_quic_buffer_append(&entry->rx_buffer, (const char *)data, datalen);
  }

  if (!entry->readable && ZSTR_LEN(entry->rx_buffer) > 0) {
    entry->readable = 1;
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_READABLE, stream_id, 0, 1,
                                   NULL);
  }

  if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
    entry->fin_recv = 1;
  }

  return 0;
}

static int php_quic_stream_close_cb(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id,
                                    uint64_t app_error_code, void *user_data,
                                    void *stream_user_data) {
  php_quic_connection *connection = user_data;
  php_quic_stream_entry *entry;
  (void)conn;
  (void)stream_user_data;

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->closed = 1;
  entry->writable = 0;

  php_quic_connection_push_event(
    connection, PHP_QUIC_EVENT_STREAM_CLOSED, stream_id,
    (flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET) ? app_error_code : 0, 1, NULL);

  return 0;
}

static int php_quic_stream_reset_cb(ngtcp2_conn *conn, int64_t stream_id,
                                    uint64_t final_size, uint64_t app_error_code,
                                    void *user_data, void *stream_user_data) {
  php_quic_connection *connection = user_data;
  php_quic_stream_entry *entry;
  (void)conn;
  (void)final_size;
  (void)stream_user_data;

  entry = php_quic_connection_open_stream_entry(connection, stream_id);
  entry->reset = 1;
  entry->closed = 1;
  entry->writable = 0;

  php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_RESET, stream_id,
                                 app_error_code, 1, NULL);
  return 0;
}

static int php_quic_acked_stream_data_offset_cb(ngtcp2_conn *conn, int64_t stream_id,
                                                uint64_t offset, uint64_t datalen,
                                                void *user_data,
                                                void *stream_user_data) {
  php_quic_connection *connection = user_data;
  php_quic_stream_entry *entry;
  (void)conn;
  (void)offset;
  (void)datalen;
  (void)stream_user_data;

  entry = php_quic_connection_get_stream_entry(connection, stream_id);
  if (entry == NULL || entry->closed || entry->reset || entry->fin_sent) {
    return 0;
  }

  if (!entry->writable) {
    entry->writable = 1;
    php_quic_connection_push_event(connection, PHP_QUIC_EVENT_STREAM_WRITABLE, stream_id, 0, 1,
                                   NULL);
  }

  return 0;
}

void php_quic_callbacks_init(ngtcp2_callbacks *callbacks) {
  memset(callbacks, 0, sizeof(*callbacks));
  callbacks->recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
  callbacks->client_initial = ngtcp2_crypto_client_initial_cb;
  callbacks->recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
  callbacks->handshake_completed = php_quic_handshake_completed_cb;
  callbacks->encrypt = ngtcp2_crypto_encrypt_cb;
  callbacks->decrypt = ngtcp2_crypto_decrypt_cb;
  callbacks->hp_mask = ngtcp2_crypto_hp_mask_cb;
  callbacks->recv_stream_data = php_quic_recv_stream_data_cb;
  callbacks->acked_stream_data_offset = php_quic_acked_stream_data_offset_cb;
  callbacks->stream_open = php_quic_stream_open_cb;
  callbacks->stream_close = php_quic_stream_close_cb;
  callbacks->stream_reset = php_quic_stream_reset_cb;
  callbacks->recv_retry = ngtcp2_crypto_recv_retry_cb;
  callbacks->rand = php_quic_rand_cb;
  callbacks->get_new_connection_id = php_quic_get_new_connection_id_cb;
  callbacks->update_key = ngtcp2_crypto_update_key_cb;
  callbacks->delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  callbacks->delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  callbacks->get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
  callbacks->version_negotiation = ngtcp2_crypto_version_negotiation_cb;
}
