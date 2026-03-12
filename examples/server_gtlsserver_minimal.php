<?php

declare(strict_types=1);

function usage(): void
{
    fwrite(STDERR, <<<TXT
Usage:
  php examples/server_gtlsserver_minimal.php [--host=127.0.0.1] [--port=4433] [--docroot=.]
                                            [--cert=/tmp/ngtcp2/server.crt] [--key=/tmp/ngtcp2/server.key]

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

if (!is_executable('/usr/sbin/gtlsserver')) {
    throw new RuntimeException('/usr/sbin/gtlsserver is not available');
}
if (!is_executable('/usr/bin/openssl')) {
    throw new RuntimeException('/usr/bin/openssl is not available');
}

$options = getopt('', ['host::', 'port::', 'docroot::', 'cert::', 'key::', 'help']);
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
$docroot = is_string($options['docroot'] ?? null) ? $options['docroot'] : getcwd();
$cert = is_string($options['cert'] ?? null) ? $options['cert'] : '/tmp/ngtcp2/server.crt';
$key = is_string($options['key'] ?? null) ? $options['key'] : '/tmp/ngtcp2/server.key';

if ($port <= 0 || $port > 65535) {
    throw new InvalidArgumentException("invalid --port: {$port}");
}
if (!is_dir($docroot)) {
    throw new InvalidArgumentException("docroot does not exist: {$docroot}");
}

ensureCertificate($cert, $key, $host);

$command = sprintf(
    '/usr/sbin/gtlsserver %s %d %s %s --quiet -d %s',
    escapeshellarg($host),
    $port,
    escapeshellarg($key),
    escapeshellarg($cert),
    escapeshellarg($docroot)
);

$descriptorSpec = [
    0 => ['pipe', 'r'],
    1 => STDOUT,
    2 => STDERR,
];

$process = proc_open($command, $descriptorSpec, $pipes);
if (!is_resource($process)) {
    throw new RuntimeException('failed to start gtlsserver');
}

fclose($pipes[0]);

$cleanup = static function () use (&$process): void {
    if (!is_resource($process)) {
        return;
    }
    $status = proc_get_status($process);
    if ($status['running']) {
        proc_terminate($process);
        usleep(300000);
    }
    proc_close($process);
};

register_shutdown_function($cleanup);

fwrite(STDERR, "gtlsserver started on {$host}:{$port}\n");
fwrite(STDERR, "docroot={$docroot}\n");
fwrite(STDERR, "cert={$cert}\n");
fwrite(STDERR, "key={$key}\n");
fwrite(STDERR, "Stop with Ctrl+C.\n");

while (true) {
    $status = proc_get_status($process);
    if (!$status['running']) {
        break;
    }
    usleep(200000);
}
