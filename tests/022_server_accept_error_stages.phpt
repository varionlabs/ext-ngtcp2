--TEST--
ServerConnection::accept exposes stage prefixes in error messages
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
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConnection;

try {
    ServerConnection::accept(new Datagram('x', new Address('127.0.0.1', 4433)));
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConnection::accept [decode]'));
}

try {
    $client = new Connection(new Address('127.0.0.1', 4433));
    $initial = ($client->flush())[0] ?? null;
    if ($initial === null) {
        throw new RuntimeException('failed to build initial datagram');
    }

    ServerConnection::accept(
        $initial,
        null,
        ['keyFile' => '/tmp/nope.key']
    );
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConnection::accept [options]'));
}
?>
--EXPECT--
bool(true)
bool(true)
