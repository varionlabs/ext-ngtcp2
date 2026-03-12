--TEST--
ServerConfig exposes typed server accept configuration
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\ServerConfig;

$local = new Address('0.0.0.0', 4433);
$config = (new ServerConfig())
    ->withLocalAddress($local)
    ->withCertificate('/tmp/server.crt', '/tmp/server.key')
    ->withAlpn('h3');

var_dump($config instanceof ServerConfig);
var_dump($config->getLocalAddress() instanceof Address);
var_dump($config->getCertFile() === '/tmp/server.crt');
var_dump($config->getKeyFile() === '/tmp/server.key');
var_dump($config->getAlpn() === 'h3');
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
