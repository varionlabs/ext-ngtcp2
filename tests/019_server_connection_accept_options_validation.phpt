--TEST--
ServerConnection::accept validates ServerConfig cert/key requirements
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
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerConnection;

$client = new Connection(new Address('127.0.0.1', 4433));
$initial = ($client->drainOutgoingDatagrams())[0] ?? null;

try {
    ServerConnection::accept(
        $initial,
        (new ServerConfig())->withAlpn('h3')
    );
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConfig.certFile and ServerConfig.keyFile are required'));
}

try {
    ServerConnection::accept(
        $initial,
        (new ServerConfig(null, '/tmp/nope.crt', null, 'h3'))
    );
    echo "no-exception\n";
} catch (Throwable $e) {
    var_dump(str_contains($e->getMessage(), 'ServerConfig.certFile and ServerConfig.keyFile are required'));
}
?>
--EXPECT--
bool(true)
bool(true)
