--TEST--
ServerConnection::accept reports [ngtcp2:new] when forced by test env
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
    return;
}
if (!is_executable('/usr/bin/openssl')) {
    echo 'skip /usr/bin/openssl is not available';
    return;
}
?>
--FILE--
<?php

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\ServerConnection;

$dir = sys_get_temp_dir() . '/ngtcp2-srv-' . bin2hex(random_bytes(4));
if (!mkdir($dir, 0700, true) && !is_dir($dir)) {
    throw new RuntimeException("failed to create temp dir: {$dir}");
}
$cert = $dir . '/server.crt';
$key = $dir . '/server.key';

try {
    $cmd = sprintf(
        '/usr/bin/openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj %s -keyout %s -out %s',
        escapeshellarg('/CN=127.0.0.1'),
        escapeshellarg($key),
        escapeshellarg($cert)
    );
    exec($cmd . ' 2>/dev/null', $out, $rc);
    if ($rc !== 0) {
        throw new RuntimeException('openssl certificate generation failed');
    }

    $client = new Connection(new Address('127.0.0.1', 4433));
    $initial = ($client->drainOutgoingDatagrams())[0] ?? null;
    if ($initial === null) {
        throw new RuntimeException('failed to build initial datagram');
    }

    putenv('NGTCP2_TEST_FORCE_SERVER_NEW_FAILURE=1');
    try {
        ServerConnection::accept($initial, null, [
            'certFile' => $cert,
            'keyFile' => $key,
            'alpn' => 'h3',
        ]);
        echo "no-exception\n";
    } catch (Throwable $e) {
        var_dump(str_contains($e->getMessage(), 'ServerConnection::accept [ngtcp2:new]'));
    } finally {
        putenv('NGTCP2_TEST_FORCE_SERVER_NEW_FAILURE');
    }
} finally {
    @unlink($cert);
    @unlink($key);
    @rmdir($dir);
}
?>
--EXPECT--
bool(true)
