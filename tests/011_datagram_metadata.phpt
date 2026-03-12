--TEST--
Datagram exposes peer/local and optional metadata fields
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;

$peer = new Address('127.0.0.1', 4433);
$local = new Address('127.0.0.1', 0);

$datagram = new Datagram('abc', $peer, $local, 3, 1234567890);
var_dump($datagram->getPayload() === 'abc');
var_dump($datagram->getPeerAddress() instanceof Address);
var_dump($datagram->getPeerAddress()->getHost() === '127.0.0.1');
var_dump($datagram->getLocalAddress() instanceof Address);
var_dump($datagram->getEcn() === 3);
var_dump($datagram->getReceivedAt() === 1234567890);

$minimal = new Datagram('x', $peer);
var_dump($minimal->getLocalAddress() === null);
var_dump($minimal->getEcn() === null);
var_dump($minimal->getReceivedAt() === null);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
