--TEST--
timeout deadline API returns absolute UNIX epoch milliseconds
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

var_dump(method_exists($conn, 'getNextExpiry'));
var_dump(method_exists($conn, 'getTimeoutAt'));
var_dump(method_exists(ServerConnection::class, 'getNextExpiry'));
var_dump(method_exists(ServerConnection::class, 'getTimeoutAt'));

$timeout = $conn->getNextTimeout();
$expiry = $conn->getNextExpiry();
$timeoutAt = $conn->getTimeoutAt();

var_dump(is_int($timeout) || $timeout === null);
var_dump(is_int($expiry) || $expiry === null);
var_dump(is_int($timeoutAt) || $timeoutAt === null);

if ($timeout === null) {
    var_dump($expiry === null);
    var_dump($timeoutAt === null);
} else {
    $nowMs = (int) floor(microtime(true) * 1000);
    var_dump($expiry >= $nowMs);
    var_dump($timeoutAt >= $nowMs);
}

$conn->close();
var_dump($conn->getNextExpiry() === null);
var_dump($conn->getTimeoutAt() === null);
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
bool(true)
bool(true)
