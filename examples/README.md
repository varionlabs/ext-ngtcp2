# Examples

## Minimal native server

`ServerConnection::accept(...)` is available as an experimental server-mode entry.
You can run the minimal native server with:

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
`server_minimal.php` also uses `ServerConnection::getNextExpiry()` to drive timer waits.
Outgoing server datagrams are sent to `(string)$outgoing->getPeerAddress()`.

Optional flags:

- `--cert=/path/to/server.crt`
- `--key=/path/to/server.key`
- `--alpn=h3`

If cert/key are missing, the script generates a temporary self-signed certificate via `openssl`.

## Legacy wrapper (gtlsserver)

For conservative local testing against external `/usr/sbin/gtlsserver`, use:

```sh
php examples/server_gtlsserver_minimal.php --host=127.0.0.1 --port=4433
```

To test stream read/write behavior on the same connection, use the echo variant:

```sh
php examples/server_native_echo.php --host=127.0.0.1 --port=4433 --alpn=h3 --prefix='echo: '
```

`server_minimal.php` / `server_native_echo.php` print `recv warning:`,
`timeout warning:`, and `drainOutgoingDatagrams warning:` during close/draining transitions.
These warnings are expected in the current MVP path and are non-fatal.

This is still an MVP path and not feature-complete server mode.
