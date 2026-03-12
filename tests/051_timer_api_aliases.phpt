--TEST--
timer API aliases are available on Connection and ServerConnection
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

$conn = new Connection(new Address('127.0.0.1', 4433));

var_dump(method_exists($conn, 'onTimeout'));
var_dump(method_exists($conn, 'handleTimers'));
var_dump(method_exists($conn, 'tick'));

$conn->close();
$conn->handleTimers();
$conn->tick();
var_dump(true);

var_dump(method_exists(ServerConnection::class, 'onTimeout'));
var_dump(method_exists(ServerConnection::class, 'handleTimers'));
var_dump(method_exists(ServerConnection::class, 'tick'));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
