--TEST--
drainEvents returns queued stream events in order
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
use Varion\Nghttp2\Events\StreamOpened;
use Varion\Nghttp2\Events\StreamWritable;

$conn = new Connection(new Address('127.0.0.1', 4433));
$conn->openStream();
$events = $conn->drainEvents();

var_dump($events[0] instanceof StreamOpened);
var_dump($events[1] instanceof StreamWritable);
var_dump(count($conn->drainEvents()) === 0);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
