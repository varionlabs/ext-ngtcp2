--TEST--
stream write/read/end API behaves consistently
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Ngtcp2\Address;
use Ngtcp2\Connection;

$conn = new Connection(new Address('127.0.0.1', 4433));
$stream = $conn->openStream();

var_dump($stream->getId() === 0);
var_dump($stream->write('hello') === 5);
$stream->end("!");
var_dump($stream->isWritable());
var_dump($stream->read() === '');
?>
--EXPECT--
bool(true)
bool(true)
bool(false)
bool(true)
