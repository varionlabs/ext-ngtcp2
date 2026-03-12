--TEST--
ServerConnection::accept reports [tls] stage on certificate initialization failure
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\ServerConnection;

$client = new Connection(new Address('127.0.0.1', 4433));
$initial = ($client->drainOutgoingDatagrams())[0] ?? null;
if ($initial === null) {
    throw new RuntimeException('failed to build initial datagram');
}

try {
    @ServerConnection::accept($initial, null, [
        'certFile' => '/tmp/does-not-exist-server.crt',
        'keyFile' => '/tmp/does-not-exist-server.key',
        'alpn' => 'h3',
    ]);
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConnection::accept [tls]'));
}
?>
--EXPECT--
bool(true)
