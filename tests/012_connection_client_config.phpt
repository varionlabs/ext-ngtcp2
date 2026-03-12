--TEST--
Connection accepts ClientConfig for localAddress/serverName/alpn
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\ClientConfig;
use Varion\Ngtcp2\Connection;

$local = new Address('0.0.0.0', 0);
$config = (new ClientConfig())
    ->withLocalAddress($local)
    ->withServerName('localhost')
    ->withAlpn('h3');

$conn = new Connection(new Address('127.0.0.1', 4433), $config);

var_dump($config instanceof ClientConfig);
var_dump($config->getLocalAddress() instanceof Address);
var_dump($config->getServerName() === 'localhost');
var_dump($config->getAlpn() === 'h3');
var_dump($conn instanceof Connection);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
