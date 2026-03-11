--TEST--
event base classes are abstract and cannot be instantiated
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Event;
use Varion\Nghttp2\Events\ConnectionEvent;
use Varion\Nghttp2\Events\StreamEvent;
use Varion\Nghttp2\Events\TerminalStreamEvent;

foreach ([Event::class, ConnectionEvent::class, StreamEvent::class, TerminalStreamEvent::class] as $class) {
    try {
        new $class(1);
        echo "no-exception\n";
    } catch (Throwable $e) {
        var_dump(str_contains($e->getMessage(), 'Cannot instantiate abstract class'));
    }
}
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
