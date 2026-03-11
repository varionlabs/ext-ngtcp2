PHP_ARG_ENABLE([ngtcp2],
  [whether to enable ngtcp2 support],
  [AS_HELP_STRING([--enable-ngtcp2], [Enable ngtcp2 extension])],
  [no])

if test "$PHP_NGTCP2" != "no"; then
  AC_PATH_PROG([PKG_CONFIG], [pkg-config], [no])
  if test "$PKG_CONFIG" = "no"; then
    AC_MSG_ERROR([pkg-config is required to build ngtcp2 extension])
  fi

  if ! $PKG_CONFIG --exists libngtcp2 libngtcp2_crypto_gnutls gnutls; then
    AC_MSG_ERROR([Missing dependency: libngtcp2, libngtcp2_crypto_gnutls, and gnutls are required])
  fi

  NGTCP2_CFLAGS=`$PKG_CONFIG --cflags libngtcp2 libngtcp2_crypto_gnutls gnutls`
  NGTCP2_LIBS=`$PKG_CONFIG --libs libngtcp2 libngtcp2_crypto_gnutls gnutls`

  PHP_EVAL_INCLINE([$NGTCP2_CFLAGS])
  PHP_EVAL_LIBLINE([$NGTCP2_LIBS], [NGTCP2_SHARED_LIBADD])

  PHP_SUBST([NGTCP2_SHARED_LIBADD])

  PHP_NEW_EXTENSION([ngtcp2], [
    ngtcp2.c
    src/address.c
    src/buffer.c
    src/callbacks.c
    src/connection.c
    src/datagram.c
    src/event.c
    src/queue.c
    src/server_connection.c
    src/stream.c
    src/tls_gnutls.c
  ], [$ext_shared])
fi
