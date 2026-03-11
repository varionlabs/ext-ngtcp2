--TEST--
ServerConnection::accept validates cert/key options
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
$initial = ($client->flush())[0] ?? null;

try {
    ServerConnection::accept($initial, null, ['keyFile' => '/tmp/nope.key']);
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), "options['certFile']"));
}

try {
    ServerConnection::accept($initial, null, ['certFile' => '/tmp/nope.crt']);
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), "options['keyFile']"));
}
?>
--EXPECT--
bool(true)
bool(true)
