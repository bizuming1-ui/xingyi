#!/usr/bin/env python3
"""Local test CA/server only; no production credentials or internet required."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parent.parent
mbed = os.environ.get('MBEDTLS_DIR')
if not mbed:
    sys.exit('Set MBEDTLS_DIR to the built mbedTLS 2.28.10 source directory')
mbed = Path(mbed).resolve()

def run(*cmd, **kwargs):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kwargs)

with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'),
        str(root / 'tests/mbedtls_session.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test'))
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'),
        str(root / 'tests/verified_connect.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test-connect'))
    for name in ('ca', 'wrong'):
        run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
            '-subj', '/CN=Local test CA', '-keyout', str(p / (name + '.key')),
            '-out', str(p / (name + '.crt')))
    run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=project123.supabase.co',
        '-keyout', str(p / 'server.key'), '-out', str(p / 'server.csr'))
    (p / 'ext').write_text('subjectAltName=DNS:project123.supabase.co\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'x509', '-req', '-in', str(p / 'server.csr'), '-CA', str(p / 'ca.crt'),
        '-CAkey', str(p / 'ca.key'), '-CAcreateserial', '-days', '1', '-extfile', str(p / 'ext'),
        '-out', str(p / 'server.crt'))
    run('openssl', 'x509', '-req', '-in', str(p / 'server.csr'), '-CA', str(p / 'ca.crt'),
        '-CAkey', str(p / 'ca.key'), '-CAcreateserial', '-days', '0', '-extfile', str(p / 'ext'),
        '-out', str(p / 'expired.crt'))
    time.sleep(2)  # ensure the zero-day certificate is in the past

    def exercise(cert, expired=False):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        server = subprocess.Popen(['openssl', 's_server', '-accept', '127.0.0.1:' + str(port),
                                   '-cert', str(p / cert), '-key', str(p / 'server.key'),
                                   '-quiet', '-www'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('test TLS server exited before becoming ready')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.02)
            else:
                raise RuntimeError('test TLS server did not start')
            args = [str(p / 'test'), str(port), str(p / 'ca.crt'), str(p / 'wrong.crt')]
            if expired:
                args.append('expired')
            subprocess.run(args, check=True, timeout=40)
            if not expired:
                subprocess.run([str(p / 'test-connect'), str(port), str(p / 'ca.crt')], check=True, timeout=40)
        finally:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

    exercise('server.crt')
    exercise('expired.crt', expired=True)
