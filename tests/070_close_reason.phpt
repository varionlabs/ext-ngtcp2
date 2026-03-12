--TEST--
close emits ConnectionClosed event with reason and code
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
use Varion\Nghttp2\Events\ConnectionClosed;

$conn = new Connection(new Address('127.0.0.1', 4433));
$conn->close(77, 'normal-close');
$events = $conn->drainEvents();
$event = $events[0] ?? null;

var_dump($event instanceof ConnectionClosed);
var_dump($event !== null ? $event->getErrorCode() === 77 : false);
var_dump($event !== null ? $event->getReason() === 'normal-close' : false);
var_dump($conn->isClosed());
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
