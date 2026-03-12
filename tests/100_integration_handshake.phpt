--TEST--
integration handshake with gtlsserver
--SKIPIF--
<?php
if (!extension_loaded('ngtcp2')) {
    echo 'skip ngtcp2 extension is not loaded';
    return;
}
if (!is_executable('/usr/sbin/gtlsserver')) {
    echo 'skip /usr/sbin/gtlsserver is not available';
    return;
}
if (!is_executable('/usr/bin/openssl')) {
    echo 'skip /usr/bin/openssl is not available';
    return;
}
$probe = @stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
if ($probe === false) {
    echo 'skip udp bind is not available in this environment';
    return;
}
fclose($probe);
?>
--FILE--
<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Connection;
use Varion\Ngtcp2\Datagram;
use Varion\Nghttp2\Events\HandshakeCompleted;

function mktemp_dir(string $prefix): string
{
    $base = sys_get_temp_dir() . DIRECTORY_SEPARATOR . $prefix . bin2hex(random_bytes(6));
    if (!mkdir($base, 0700, true) && !is_dir($base)) {
        throw new RuntimeException("failed to create temp dir: {$base}");
    }
    return $base;
}

function rrmdir(string $dir): void
{
    if (!is_dir($dir)) {
        return;
    }
    $it = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS),
        RecursiveIteratorIterator::CHILD_FIRST
    );
    foreach ($it as $entry) {
        if ($entry->isDir()) {
            rmdir($entry->getPathname());
        } else {
            unlink($entry->getPathname());
        }
    }
    rmdir($dir);
}

$tmp = mktemp_dir('ngtcp2-it-');
$key = $tmp . '/server.key';
$crt = $tmp . '/server.crt';
$port = random_int(20000, 45000);
$udp = null;
$proc = null;
$pipes = [];

try {
    $cmd = sprintf(
        '/usr/bin/openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj %s -keyout %s -out %s',
        escapeshellarg('/CN=127.0.0.1'),
        escapeshellarg($key),
        escapeshellarg($crt)
    );
    exec($cmd . ' 2>/dev/null', $out, $rc);
    if ($rc !== 0) {
        throw new RuntimeException('openssl certificate generation failed');
    }

    $serverCmd = sprintf(
        '/usr/sbin/gtlsserver 127.0.0.1 %d %s %s --quiet',
        $port,
        escapeshellarg($key),
        escapeshellarg($crt)
    );
    $proc = proc_open(
        $serverCmd,
        [
            0 => ['pipe', 'r'],
            1 => ['pipe', 'w'],
            2 => ['pipe', 'w'],
        ],
        $pipes
    );
    if (!is_resource($proc)) {
        throw new RuntimeException('failed to start gtlsserver');
    }

    usleep(400000);

    $udp = stream_socket_client(
        "udp://127.0.0.1:{$port}",
        $errno,
        $errstr,
        1,
        STREAM_CLIENT_CONNECT
    );
    if ($udp === false) {
        throw new RuntimeException("udp connect failed: ({$errno}) {$errstr}");
    }
    stream_set_blocking($udp, false);

    $remote = new Address('127.0.0.1', $port);
    $connection = new Connection($remote);

    $handshakeCompleted = false;
    $deadline = microtime(true) + 6.0;

    while (microtime(true) < $deadline && !$connection->isClosed()) {
        foreach ($connection->drainOutgoingDatagrams() as $dgram) {
            stream_socket_sendto($udp, $dgram->getPayload());
        }

        $read = [$udp];
        $write = null;
        $except = null;
        $n = stream_select($read, $write, $except, 0, 100000);
        if ($n === false) {
            throw new RuntimeException('stream_select failed');
        }

        if ($n > 0) {
            $packet = stream_socket_recvfrom($udp, 65535);
            if (is_string($packet) && $packet !== '') {
                $connection->recv(new Datagram($packet, $remote));
            }
        } else {
            $connection->onTimeout();
        }

        foreach ($connection->drainEvents() as $event) {
            if ($event instanceof HandshakeCompleted) {
                $handshakeCompleted = true;
                break 2;
            }
        }
    }

    var_dump($handshakeCompleted);
} finally {
    if (is_resource($udp)) {
        fclose($udp);
    }

    if (is_resource($proc)) {
        proc_terminate($proc);
        foreach ($pipes as $pipe) {
            if (is_resource($pipe)) {
                fclose($pipe);
            }
        }
        proc_close($proc);
    }

    rrmdir($tmp);
}
?>
--EXPECT--
bool(true)
