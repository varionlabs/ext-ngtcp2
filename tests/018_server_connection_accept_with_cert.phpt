--TEST--
ServerConnection::accept can initialize native server with cert/key options
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
    $initial = ($client->flush())[0] ?? null;
    var_dump($initial !== null);

    $server = ServerConnection::accept($initial, null, [
        'certFile' => $cert,
        'keyFile' => $key,
        'alpn' => 'h3',
    ]);

    var_dump($server instanceof ServerConnection);
    var_dump(is_array($server->flush()));
} finally {
    @unlink($cert);
    @unlink($key);
    @rmdir($dir);
}
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
