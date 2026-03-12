# Examples

## Server wrapper (stable path)

The stable API surface is client-focused. For conservative local testing, you can
use `server_minimal.php` to run `/usr/sbin/gtlsserver` as an external QUIC server.

```sh
php examples/server_minimal.php --host=127.0.0.1 --port=4433
```

Then run the client in another terminal:

```sh
php examples/client_minimal.php
```

For a one-shot run that exits after first response payload:

```sh
php examples/client_once.php --host=127.0.0.1 --port=4433 --path=/
```

`client_minimal.php` demonstrates event-loop integration with
`Connection::getNextExpiry()` (absolute UNIX epoch milliseconds).

Optional flags:

- `--docroot=/path/to/root`
- `--cert=/path/to/server.crt`
- `--key=/path/to/server.key`

If cert/key are missing, the script generates a temporary self-signed certificate via `openssl`.

## Experimental native server entry

`ServerConnection::accept(...)` is now available as an experimental server-mode entry.
You can try it with:

```sh
php examples/server_native_minimal.php --host=127.0.0.1 --port=4433 --alpn=h3
```

To test stream read/write behavior on the same connection, use the echo variant:

```sh
php examples/server_native_echo.php --host=127.0.0.1 --port=4433 --alpn=h3 --prefix='echo: '
```

`server_native_minimal.php` / `server_native_echo.php` print `recv warning:`,
`timeout warning:`, and `drainOutgoingDatagrams warning:` during close/draining transitions.
These warnings are expected in the current MVP path and are non-fatal.

This is still an MVP path and not feature-complete server mode.
