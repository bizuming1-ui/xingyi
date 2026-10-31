#!/usr/bin/env python3
"""Local test CA/server only; no production credentials or internet required."""
import os
import hashlib
import hmac
import json
from pathlib import Path
import socket
import ssl
import threading
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parent.parent
mbed = os.environ.get('MBEDTLS_DIR')
if not mbed:
    sys.exit('Set MBEDTLS_DIR to the built mbedTLS 2.28.10 source directory')
mbed = Path(mbed).resolve()
arduino = os.environ.get('ARDUINOJSON_INCLUDE')
if not arduino:
    sys.exit('Set ARDUINOJSON_INCLUDE to ArduinoJson 6.21.5 src')

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
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'), '-I', arduino,
        str(root / 'tests/mbedtls_http.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test-http'))
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'), '-I', arduino,
        str(root / 'tests/mbedtls_keepalive.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test-keepalive'))
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'), '-I', arduino,
        str(root / 'tests/mbedtls_poll_interrupt.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test-poll-interrupt'))
    run('g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
        '-I', str(root / 'tests/mocks'), '-I', str(mbed / 'include'), '-I', arduino,
        str(root / 'tests/time_bootstrap.cpp'), '-L', str(mbed / 'library'),
        '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-o', str(p / 'test-bootstrap'))
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
    # Test expiry at every chain level, not just the server leaf. Both the
    # intermediate and root retain valid signatures and CA constraints.
    run('openssl', 'x509', '-in', str(p / 'ca.crt'), '-signkey', str(p / 'ca.key'),
        '-days', '0', '-out', str(p / 'expired-root.crt'))
    run('openssl', 'x509', '-req', '-in', str(p / 'server.csr'), '-CA', str(p / 'expired-root.crt'),
        '-CAkey', str(p / 'ca.key'), '-CAcreateserial', '-days', '1', '-extfile', str(p / 'ext'),
        '-out', str(p / 'expired-root-leaf.crt'))
    run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=Intermediate test CA',
        '-keyout', str(p / 'intermediate.key'), '-out', str(p / 'intermediate.csr'))
    (p / 'ca-ext').write_text('basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n')
    run('openssl', 'x509', '-req', '-in', str(p / 'intermediate.csr'), '-CA', str(p / 'ca.crt'),
        '-CAkey', str(p / 'ca.key'), '-CAcreateserial', '-days', '0', '-extfile', str(p / 'ca-ext'),
        '-out', str(p / 'expired-intermediate.crt'))
    run('openssl', 'x509', '-req', '-in', str(p / 'server.csr'), '-CA', str(p / 'expired-intermediate.crt'),
        '-CAkey', str(p / 'intermediate.key'), '-CAcreateserial', '-days', '1', '-extfile', str(p / 'ext'),
        '-out', str(p / 'intermediate-leaf.crt'))
    # Sign a not-yet-valid leaf using openssl ca's explicit date support.
    (p / 'newcerts').mkdir()
    (p / 'index').write_text('')
    (p / 'serial').write_text('1000\n')
    (p / 'ca.cnf').write_text('[ca]\ndefault_ca=local\n[local]\n'
        'database=' + str(p / 'index') + '\nnew_certs_dir=' + str(p / 'newcerts') +
        '\nserial=' + str(p / 'serial') + '\ncertificate=' + str(p / 'ca.crt') +
        '\nprivate_key=' + str(p / 'ca.key') +
        '\ndefault_md=sha256\npolicy=policy\nx509_extensions=server\n'
        '[policy]\ncommonName=supplied\n[server]\nsubjectAltName=DNS:project123.supabase.co\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'ca', '-batch', '-config', str(p / 'ca.cnf'), '-in', str(p / 'server.csr'),
        '-startdate', '20990101000000Z', '-enddate', '21000101000000Z', '-out', str(p / 'future.crt'))
    time.sleep(2)  # ensure the zero-day certificates are in the past

    def exercise(cert, expired=False, ca='ca.crt', chain=None):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        command = ['openssl', 's_server', '-accept', '127.0.0.1:' + str(port),
                   '-cert', str(p / cert), '-key', str(p / 'server.key'), '-quiet', '-www']
        if chain:
            command.extend(['-cert_chain', str(p / chain)])
        server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
            args = [str(p / 'test'), str(port), str(p / ca), str(p / 'wrong.crt')]
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
    exercise('future.crt', expired=True)
    exercise('expired-root-leaf.crt', expired=True, ca='expired-root.crt')
    exercise('intermediate-leaf.crt', expired=True, chain='expired-intermediate.crt')

    # Minimal local HTTPS RPC peer, with ephemeral test credentials only.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(p / 'server.crt'), str(p / 'server.key'))
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(.1)
    http_port = listener.getsockname()[1]
    stop = threading.Event()
    def serve():
        while not stop.is_set():
            try:
                raw, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                raw.settimeout(3)
                with context.wrap_socket(raw, server_side=True) as conn:
                    received = b''
                    while b'\r\n\r\n' not in received and len(received)<8192:
                        chunk = conn.recv(4096)
                        if not chunk: break
                        received += chunk
                    header, _, body = received.partition(b'\r\n\r\n')
                    lengths = [line.split(b':',1)[1].strip() for line in header.split(b'\r\n')
                               if line.lower().startswith(b'content-length:')]
                    count = int(lengths[0]) if lengths else 0
                    while len(body)<count:
                        chunk = conn.recv(4096)
                        if not chunk: break
                        body += chunk
                    if not header.startswith(b'POST /rest/v1/rpc/device_status HTTP/1.1\r\n') or b'apikey: sb_publishable_test' not in header:
                        raise OSError('unexpected request')
                    if b'stall' in body:
                        time.sleep(.5)
                        continue
                    if b'0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef' not in body:
                        raise OSError('test body not received')
                    payload = b'{"desired_order_id":null,"desired_binding_id":null,"desired_binding_revision":null}'
                    conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: '+str(len(payload)).encode()+b'\r\n\r\n'+payload)
            except (OSError, ssl.SSLError, ValueError):
                raw.close()
    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        certificate = ssl._ssl._test_decode_cert(str(p / 'server.crt'))
        not_before = int(ssl.cert_time_to_seconds(certificate['notBefore']))
        not_after = int(ssl.cert_time_to_seconds(certificate['notAfter']))
        subprocess.run([str(p / 'test-http'), str(http_port), str(p / 'ca.crt'), str(p / 'wrong.crt'),
                        str(not_before), str(not_after)], check=True, timeout=30)
    finally:
        stop.set()
        listener.close()
        thread.join(timeout=3)

    # A verified HTTPS connection serves two sequential framed requests. Test
    # date-window invalidation before reuse; no real credentials are involved.
    listener = socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(10)
    keep_port=listener.getsockname()[1];received_requests=[];server_errors=[]
    def keep_server():
        try:
            raw,_=listener.accept();raw.settimeout(5)
            with context.wrap_socket(raw,server_side=True) as conn:
                buf=b''
                for _ in range(2):
                    while b'\r\n\r\n' not in buf: buf+=conn.recv(4096)
                    head,_,body=buf.partition(b'\r\n\r\n')
                    count=int(next(line.split(b':',1)[1] for line in head.split(b'\r\n') if line.lower().startswith(b'content-length:')))
                    while len(body)<count:body+=conn.recv(4096)
                    assert b'Connection: keep-alive' in head and body[:count]==b'{}'
                    received_requests.append(1);buf=body[count:]
                    conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}')
        except Exception as e:server_errors.append(str(e))
    thread=threading.Thread(target=keep_server,daemon=True);thread.start()
    try:
        subprocess.run([str(p/'test-keepalive'),str(keep_port),str(p/'ca.crt')],check=True,timeout=20)
        thread.join(timeout=6);assert not server_errors,server_errors
        assert len(received_requests)==2
    finally:listener.close()

    # A real verified TLS peer accepts the status request but stalls. A
    # simulated locally accepted count interrupts only the receive and closes
    # the connection without treating the unknown response as success.
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(10)
    interrupt_port=listener.getsockname()[1];interrupt_errors=[];interrupt_requests=[]
    def interrupt_server():
        try:
            raw,_=listener.accept();raw.settimeout(5)
            with context.wrap_socket(raw,server_side=True) as conn:
                data=b''
                while b'\r\n\r\n' not in data:
                    part=conn.recv(4096)
                    if not part:raise RuntimeError('missing request')
                    data+=part
                interrupt_requests.append(1)
                time.sleep(1)
        except Exception as e:interrupt_errors.append(str(e))
    thread=threading.Thread(target=interrupt_server,daemon=True);thread.start()
    try:
        subprocess.run([str(p/'test-poll-interrupt'),str(interrupt_port),str(p/'ca.crt')],check=True,timeout=15)
        thread.join(timeout=5);assert not interrupt_errors,interrupt_errors
        assert len(interrupt_requests)==1
    finally:listener.close()

    # Expired test leaf: date exception applies only to this restricted endpoint;
    # CA and host validation still apply and the signed reply is mandatory.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(p / 'expired.crt'), str(p / 'server.key'))
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(.1)
    port = listener.getsockname()[1]
    stop = threading.Event()
    requests = []
    fixture_secret = '0123456789abcdef' * 4
    key = hashlib.sha256(fixture_secret.encode()).digest()
    def time_server():
        while not stop.is_set():
            try:
                raw, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                raw.settimeout(3)
                with context.wrap_socket(raw, server_side=True) as conn:
                    data = b''
                    while b'\r\n\r\n' not in data and len(data)<8192:
                        part=conn.recv(4096)
                        if not part: break
                        data += part
                    header, sep, body = data.partition(b'\r\n\r\n')
                    sizes=[line.split(b':',1)[1].strip() for line in header.split(b'\r\n')
                           if line.lower().startswith(b'content-length:')]
                    if not sep or len(sizes)!=1: continue
                    count=int(sizes[0])
                    while len(body)<count:
                        part=conn.recv(4096)
                        if not part: break
                        body+=part
                    if b'Host: project123.supabase.co' not in header or b'apikey: sb_publishable_test' not in header:
                        raise ValueError('invalid public host or key')
                    payload=json.loads(body)
                    if header.startswith(b'POST /rest/v1/rpc/device_status HTTP/1.1\r\n'):
                        if payload.get('p_secret') != fixture_secret:
                            raise ValueError('normal RPC test credential missing')
                        reply=b'{"desired_order_id":null,"desired_binding_id":null,"desired_binding_revision":null}'
                        requests.append({'rpc':'status'})
                        conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: '+str(len(reply)).encode()+b'\r\n\r\n'+reply)
                        continue
                    if not header.startswith(b'POST /rest/v1/rpc/device_time_proof HTTP/1.1\r\n'):
                        raise ValueError('bootstrap endpoint must be fixed')
                    if set(payload)!={'p_device','p_nonce'} or fixture_secret.encode() in data:
                        raise ValueError('bootstrap must never transmit a secret')
                    requests.append(payload)
                    nonce=payload['p_nonce']
                    if nonce.startswith('10'):
                        time.sleep(.5)
                        continue
                    if nonce.startswith('12'):
                        time.sleep(3.3)
                    if nonce.startswith('13'):
                        time.sleep(5.2)
                    epoch=str(int(time.time()))
                    message=('xingyi-time-v1|'+payload['p_device']+'|'+nonce+'|'+epoch).encode()
                    tag=hmac.new(key,message,hashlib.sha256).hexdigest()
                    if nonce.startswith('11'): tag='0'*64
                    reply=json.dumps({'unix_seconds':epoch,'tag':tag}).encode()
                    conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: '+str(len(reply)).encode()+b'\r\n\r\n'+reply)
            except (OSError, ssl.SSLError, ValueError):
                raw.close()
    thread=threading.Thread(target=time_server,daemon=True)
    thread.start()
    try:
        subprocess.run([str(p / 'test-bootstrap'),str(port),str(p / 'ca.crt'),str(p / 'wrong.crt')],check=True,timeout=30)
        assert len(requests)==12, f'unexpected time-only requests: {len(requests)}'
    finally:
        stop.set()
        listener.close()
        thread.join(timeout=3)

    # Repeat against a valid certificate, then send a normal authenticated RPC
    # using the newly verified time window and the standard TLS channel.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(p / 'server.crt'), str(p / 'server.key'))
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(.1)
    port = listener.getsockname()[1]
    stop = threading.Event()
    requests = []
    thread = threading.Thread(target=time_server,daemon=True)
    thread.start()
    try:
        subprocess.run([str(p / 'test-bootstrap'),str(port),str(p / 'ca.crt'),str(p / 'wrong.crt'),'full'],check=True,timeout=30)
        assert len(requests)==2 and 'p_nonce' in requests[0] and requests[1]=={'rpc':'status'}
    finally:
        stop.set()
        listener.close()
        thread.join(timeout=3)
