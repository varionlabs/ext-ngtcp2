#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "internal/buffer.h"

void php_quic_buffer_append(zend_string **buffer, const char *data, size_t len) {
  size_t current_len;

  if (len == 0) {
    return;
  }

  if (*buffer == NULL) {
    *buffer = zend_string_init("", 0, 0);
  }

  current_len = ZSTR_LEN(*buffer);
  *buffer = zend_string_extend(*buffer, current_len + len, 0);
  memcpy(ZSTR_VAL(*buffer) + current_len, data, len);
  ZSTR_VAL(*buffer)[current_len + len] = '\0';
}

zend_string *php_quic_buffer_read(zend_string **buffer, size_t max_len) {
  zend_string *chunk;
  size_t buffer_len;
  size_t read_len;

  if (*buffer == NULL || ZSTR_LEN(*buffer) == 0 || max_len == 0) {
    return zend_string_init("", 0, 0);
  }

  buffer_len = ZSTR_LEN(*buffer);
  read_len = max_len < buffer_len ? max_len : buffer_len;
  chunk = zend_string_init(ZSTR_VAL(*buffer), read_len, 0);

  if (read_len == buffer_len) {
    zend_string_release(*buffer);
    *buffer = zend_string_init("", 0, 0);
    return chunk;
  }

  memmove(ZSTR_VAL(*buffer), ZSTR_VAL(*buffer) + read_len, buffer_len - read_len);
  *buffer = zend_string_truncate(*buffer, buffer_len - read_len, 0);
  ZSTR_VAL(*buffer)[buffer_len - read_len] = '\0';

  return chunk;
}
