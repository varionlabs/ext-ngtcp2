<?php

declare(strict_types=1);

use Varion\Ngtcp2\Address;
use Varion\Ngtcp2\Datagram;
use Varion\Ngtcp2\ServerConfig;
use Varion\Ngtcp2\ServerConnection;

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/server_minimal.php [--host=127.0.0.1] [--port=4433]
                                 [--cert=/tmp/ngtcp2/server.crt] [--key=/tmp/ngtcp2/server.key]
                                 [--alpn=h3]

TXT);
}

function ensureCertificate(string $certPath, string $keyPath, string $host): void
{
    if (is_file($certPath) && is_file($keyPath)) {
        return;
    }

    $dir = dirname($certPath);
    if (!is_dir($dir) && !mkdir($dir, 0700, true) && !is_dir($dir)) {
        throw new RuntimeException("failed to create certificate directory: {$dir}");
    }

    $dir = dirname($keyPath);
    if (!is_dir($dir) && !mkdir($dir, 0700, true) && !is_dir($dir)) {
        throw new RuntimeException("failed to create key directory: {$dir}");
    }

    $subject = '/CN=' . $host;
    $command = sprintf(
        '/usr/bin/openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 -subj %s -keyout %s -out %s',
        escapeshellarg($subject),
        escapeshellarg($keyPath),
        escapeshellarg($certPath)
    );
    exec($command . ' 2>/dev/null', $out, $code);
    if ($code !== 0) {
        throw new RuntimeException('openssl certificate generation failed');
    }
}

function parsePeerAddress(string $peer): Address
{
    if (preg_match('/^\[(.+)\]:(\d+)$/', $peer, $m) === 1) {
        return new Address($m[1], (int)$m[2]);
    }

    $pos = strrpos($peer, ':');
    if ($pos === false) {
        throw new RuntimeException("cannot parse peer address: {$peer}");
    }

    return new Address(substr($peer, 0, $pos), (int)substr($peer, $pos + 1));
}

function nowMilliseconds(): int
{
    return (int) floor(microtime(true) * 1000);
}

$options = getopt('', ['host::', 'port::', 'cert::', 'key::', 'alpn::', 'help']);
if ($options === false) {
    usage();
    exit(2);
}
if (isset($options['help'])) {
    usage();
    exit(0);
}

$host = is_string($options['host'] ?? null) ? $options['host'] : '127.0.0.1';
$port = (int)($options['port'] ?? 4433);
$cert = is_string($options['cert'] ?? null) ? $options['cert'] : '/tmp/ngtcp2/server.crt';
$key = is_string($options['key'] ?? null) ? $options['key'] : '/tmp/ngtcp2/server.key';
$alpn = is_string($options['alpn'] ?? null) ? $options['alpn'] : 'h3';

if ($port <= 0 || $port > 65535) {
    throw new InvalidArgumentException("invalid --port: {$port}");
}
if (!is_executable('/usr/bin/openssl')) {
    throw new RuntimeException('/usr/bin/openssl is not available');
}

ensureCertificate($cert, $key, $host);

$udp = stream_socket_server(
    "udp://{$host}:{$port}",
    $errno,
    $errstr,
    STREAM_SERVER_BIND
);
if ($udp === false) {
    throw new RuntimeException("failed to bind UDP socket: ({$errno}) {$errstr}");
}
stream_set_blocking($udp, false);

fwrite(STDERR, "native server waiting on {$host}:{$port}\n");

$peer = null;
while (true) {
    $read = [$udp];
    $write = null;
    $except = null;
    $n = stream_select($read, $write, $except, 0, 100000);
    if ($n === false) {
        throw new RuntimeException('stream_select failed');
    }
    if ($n === 0) {
        continue;
    }

    $packet = stream_socket_recvfrom($udp, 65535, 0, $peer);
    if (is_string($packet) && $packet !== '' && is_string($peer) && $peer !== '') {
        break;
    }
}

$remote = parsePeerAddress($peer);
$localName = stream_socket_get_name($udp, false);
$local = parsePeerAddress($localName === false ? "{$host}:{$port}" : $localName);

$server = ServerConnection::accept(
    new Datagram($packet, $remote, $local),
    (new ServerConfig())
        ->withCertificate($cert, $key)
        ->withAlpn($alpn)
);

foreach ($server->drainOutgoingDatagrams() as $outgoing) {
    stream_socket_sendto(
        $udp,
        $outgoing->getPayload(),
        0,
        (string)$outgoing->getPeerAddress()
    );
}

$deadline = microtime(true) + 10.0;
$idlePollMs = 100;
while (microtime(true) < $deadline && !$server->isClosed()) {
    $timeoutAt = $server->getNextExpiry();
    if ($timeoutAt === null) {
        $timeoutMs = $idlePollMs;
    } else {
        $timeoutMs = max(0, $timeoutAt - nowMilliseconds());
    }

    $read = [$udp];
    $write = null;
    $except = null;
    $sec = intdiv($timeoutMs, 1000);
    $usec = ($timeoutMs % 1000) * 1000;
    $n = stream_select($read, $write, $except, $sec, $usec);
    if ($n === false) {
        throw new RuntimeException('stream_select failed');
    }

    if ($n > 0) {
        $packet = stream_socket_recvfrom($udp, 65535, 0, $from);
        if (is_string($packet) && $packet !== '' && is_string($from) && $from !== '') {
            try {
                $server->recv(new Datagram($packet, parsePeerAddress($from), $local));
            } catch (Throwable $e) {
                fwrite(STDERR, "recv warning: {$e->getMessage()}\n");
            }
        }
    } else {
        try {
            $server->handleTimers();
        } catch (Throwable $e) {
            fwrite(STDERR, "timeout warning: {$e->getMessage()}\n");
        }
    }

    try {
        foreach ($server->drainOutgoingDatagrams() as $outgoing) {
            stream_socket_sendto(
                $udp,
                $outgoing->getPayload(),
                0,
                (string)$outgoing->getPeerAddress()
            );
        }
    } catch (Throwable $e) {
        fwrite(STDERR, "drainOutgoingDatagrams warning: {$e->getMessage()}\n");
    }

    foreach ($server->drainEvents() as $event) {
        fwrite(STDERR, get_class($event) . PHP_EOL);
    }
}

fclose($udp);
