--TEST--
ngtcp2 extension loads
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
}
?>
--FILE--
<?php
var_dump(extension_loaded('ngtcp2'));
?>
--EXPECT--
bool(true)
