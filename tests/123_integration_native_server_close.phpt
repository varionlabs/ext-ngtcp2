--TEST--
integration native ServerConnection close is observable on client side
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
use Varion\Nghttp2\Events\ConnectionClosed;
use Varion\Ngtcp2\Datagram;
use Varion\Nghttp2\Events\HandshakeCompleted;
use Varion\Ngtcp2\ServerConnection;

function parseAddr(string $peer): Address
{
    if (preg_match('/^\[(.+)\]:(\d+)$/', $peer, $m) === 1) {
        return new Address($m[1], (int)$m[2]);
    }
    $pos = strrpos($peer, ':');
    if ($pos === false) {
        throw new RuntimeException("cannot parse address: {$peer}");
    }
    return new Address(substr($peer, 0, $pos), (int)substr($peer, $pos + 1));
}

$dir = sys_get_temp_dir() . '/ngtcp2-native-' . bin2hex(random_bytes(4));
$cert = $dir . '/server.crt';
$key = $dir . '/server.key';
$serverSock = null;
$clientSock = null;

if (!mkdir($dir, 0700, true) && !is_dir($dir)) {
    throw new RuntimeException("failed to create temp dir: {$dir}");
}

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

    $serverSock = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
    if ($serverSock === false) {
        throw new RuntimeException("failed to bind UDP server socket: ({$errno}) {$errstr}");
    }
    stream_set_blocking($serverSock, false);

    $serverName = stream_socket_get_name($serverSock, false);
    if (!is_string($serverName) || $serverName === '') {
        throw new RuntimeException('failed to resolve UDP server socket name');
    }
    $serverAddr = parseAddr($serverName);

    $clientSock = stream_socket_client(
        sprintf('udp://%s:%d', $serverAddr->getHost(), $serverAddr->getPort()),
        $errno,
        $errstr,
        1,
        STREAM_CLIENT_CONNECT
    );
    if ($clientSock === false) {
        throw new RuntimeException("failed to connect UDP client socket: ({$errno}) {$errstr}");
    }
    stream_set_blocking($clientSock, false);

    $clientConn = new Connection($serverAddr);
    $serverConn = null;

    $clientHs = false;
    $serverHs = false;
    $serverClosed = false;
    $clientClosedEvent = false;
    $clientClosedState = false;
    $deadline = microtime(true) + 10.0;

    while (microtime(true) < $deadline && !$clientClosedState) {
        foreach ($clientConn->flush() as $dgram) {
            stream_socket_sendto($clientSock, $dgram->getPayload());
        }

        if ($serverConn instanceof ServerConnection) {
            foreach ($serverConn->flush() as $dgram) {
                stream_socket_sendto($serverSock, $dgram->getPayload(), 0, (string)$serverPeer);
            }
        }

        $packet = stream_socket_recvfrom($serverSock, 65535, 0, $peer);
        if (is_string($packet) && $packet !== '' && is_string($peer) && $peer !== '') {
            $serverPeer = $peer;
            $remote = parseAddr($peer);
            $local = $serverAddr;
            $dgram = new Datagram($packet, $remote, $local);

            if (!$serverConn instanceof ServerConnection) {
                $serverConn = ServerConnection::accept($dgram, $local, [
                    'certFile' => $cert,
                    'keyFile' => $key,
                    'alpn' => 'h3',
                ]);
            } else {
                $serverConn->recv($dgram);
            }
        } elseif ($serverConn instanceof ServerConnection) {
            $serverConn->onTimeout();
        }

        $cpkt = stream_socket_recvfrom($clientSock, 65535, 0, $peer2);
        if (is_string($cpkt) && $cpkt !== '') {
            $clientConn->recv(new Datagram($cpkt, $serverAddr));
        } else {
            $clientConn->onTimeout();
        }

        foreach ($clientConn->pollEvents() as $event) {
            if ($event instanceof HandshakeCompleted) {
                $clientHs = true;
            }
            if ($event instanceof ConnectionClosed) {
                $clientClosedEvent = true;
            }
        }

        if ($serverConn instanceof ServerConnection) {
            foreach ($serverConn->pollEvents() as $event) {
                if ($event instanceof HandshakeCompleted) {
                    $serverHs = true;
                }
            }

            if ($clientHs && $serverHs && !$serverClosed) {
                $serverConn->close(321, 'server-close');
                $serverClosed = true;
            }
        }

        $clientClosedState = $clientConn->isClosed();
    }

    var_dump($clientHs);
    var_dump($serverHs);
    var_dump($serverClosed);
    var_dump($clientClosedEvent);
    var_dump($clientClosedState);
} finally {
    if (is_resource($clientSock)) {
        fclose($clientSock);
    }
    if (is_resource($serverSock)) {
        fclose($serverSock);
    }
    @unlink($cert);
    @unlink($key);
    @rmdir($dir);
}
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
