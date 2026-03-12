--TEST--
Address::fromString parses IPv4 and bracketed IPv6 endpoints
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;

$v4 = Address::fromString('127.0.0.1:4433');
var_dump($v4 instanceof Address);
var_dump($v4->getHost());
var_dump($v4->getPort());

$v6 = Address::fromString('[::1]:8443');
var_dump($v6 instanceof Address);
var_dump($v6->getHost());
var_dump($v6->getPort());

try {
    Address::fromString('::1:8443');
    var_dump(false);
} catch (InvalidArgumentException $e) {
    var_dump(true);
}

try {
    Address::fromString('[::1]');
    var_dump(false);
} catch (InvalidArgumentException $e) {
    var_dump(true);
}
?>
--EXPECT--
bool(true)
string(9) "127.0.0.1"
int(4433)
bool(true)
string(3) "::1"
int(8443)
bool(true)
bool(true)
