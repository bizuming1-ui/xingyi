/**
 * 极简 MQTT 3.1.1 订阅客户端（WebSocket 传输），只做“连接 + 订阅 + 收消息 + 心跳 + 自动重连”。
 * 不依赖任何第三方库，体积小、延迟低，GitHub Pages 纯静态网站也能直接使用。
 */
type Bytes = Uint8Array<ArrayBuffer>;

export interface MqttMessage {
  topic: string;
  payload: string;
  /** true 表示这是服务器保存的“旧消息”（刚订阅时收到），false 表示实时消息 */
  retain: boolean;
}

export interface MqttOptions {
  url: string;
  topics: string[];
  onMessage: (m: MqttMessage) => void;
  onState: (connected: boolean) => void;
}

const te = new TextEncoder();
const td = new TextDecoder();

function utf(s: string): Bytes {
  const b = te.encode(s);
  const out = new Uint8Array(2 + b.length);
  out[0] = b.length >> 8;
  out[1] = b.length & 255;
  out.set(b, 2);
  return out;
}

function join(parts: Bytes[]): Bytes {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

function frame(first: number, body: Bytes): Bytes {
  const len: number[] = [];
  let n = body.length;
  do {
    let b = n % 128;
    n = Math.floor(n / 128);
    if (n > 0) b |= 128;
    len.push(b);
  } while (n > 0);
  const out = new Uint8Array(1 + len.length + body.length);
  out[0] = first;
  out.set(len, 1);
  out.set(body, 1 + len.length);
  return out;
}

const KEEPALIVE_S = 60;

export function connectMqtt(o: MqttOptions): { close: () => void } {
  let ws: WebSocket | null = null;
  let closed = false;
  let retry = 0;
  let retryTimer: number | undefined;
  let pingTimer: number | undefined;
  let connTimer: number | undefined;
  let lastRx = 0;
  let acked = false;

  const clientId = "xb-web-" + Math.random().toString(16).slice(2, 10);

  const schedule = () => {
    if (closed) return;
    const delay = Math.min(1000 * 2 ** retry, 10000);
    retry++;
    retryTimer = window.setTimeout(open, delay);
  };

  const open = () => {
    if (closed) return;
    let sock: WebSocket;
    try {
      sock = new WebSocket(o.url, "mqtt");
    } catch {
      schedule();
      return;
    }
    ws = sock;
    sock.binaryType = "arraybuffer";
    let buf: Bytes = new Uint8Array(0);
    acked = false;
    lastRx = Date.now();

    window.clearTimeout(connTimer);
    connTimer = window.setTimeout(() => {
      if (!acked) sock.close();
    }, 8000);

    const handle = (first: number, body: Bytes) => {
      const type = first >> 4;
      if (type === 2) {
        // CONNACK
        if (body[1] === 0) {
          acked = true;
          retry = 0;
          window.clearTimeout(connTimer);
          const subs = o.topics.map((t) => join([utf(t), Uint8Array.of(0)]));
          sock.send(frame(0x82, join([Uint8Array.of(0, 1), ...subs])));
          o.onState(true);
        } else {
          sock.close();
        }
      } else if (type === 3) {
        // PUBLISH
        const qos = (first >> 1) & 3;
        const retain = (first & 1) === 1;
        const tl = (body[0] << 8) | body[1];
        let p = 2 + tl;
        const topic = td.decode(body.subarray(2, p));
        let id = 0;
        if (qos > 0) {
          id = (body[p] << 8) | body[p + 1];
          p += 2;
        }
        const payload = td.decode(body.subarray(p));
        if (qos === 1) sock.send(Uint8Array.of(0x40, 2, id >> 8, id & 255));
        o.onMessage({ topic, payload, retain });
      }
      // SUBACK(9) / PINGRESP(13) 等无需处理
    };

    const parse = () => {
      for (;;) {
        if (buf.length < 2) return;
        let mul = 1;
        let len = 0;
        let i = 1;
        let more = true;
        while (more) {
          if (i >= buf.length) return; // 长度字段还没收全
          const b = buf[i++];
          len += (b & 127) * mul;
          mul *= 128;
          more = (b & 128) !== 0;
          if (more && i > 4) {
            sock.close();
            return;
          }
        }
        if (buf.length < i + len) return; // 包体还没收全
        const first = buf[0];
        const body = buf.slice(i, i + len);
        buf = buf.slice(i + len);
        handle(first, body);
      }
    };

    sock.onopen = () => {
      if (ws !== sock) return;
      const body = join([
        utf("MQTT"),
        Uint8Array.of(4, 0x02, KEEPALIVE_S >> 8, KEEPALIVE_S & 255),
        utf(clientId),
      ]);
      sock.send(frame(0x10, body));
    };

    sock.onmessage = (ev) => {
      if (ws !== sock) return;
      lastRx = Date.now();
      const chunk = new Uint8Array(ev.data as ArrayBuffer);
      buf = join([buf, chunk]);
      parse();
    };

    sock.onclose = () => {
      if (ws !== sock) return;
      ws = null;
      window.clearTimeout(connTimer);
      if (acked) o.onState(false);
      acked = false;
      schedule();
    };

    sock.onerror = () => {
      try {
        sock.close();
      } catch {
        /* ignore */
      }
    };
  };

  // 心跳：每 20 秒发一次 PINGREQ；90 秒没收到任何数据就判定断线并重连
  pingTimer = window.setInterval(() => {
    const s = ws;
    if (!s || s.readyState !== WebSocket.OPEN || !acked) return;
    if (Date.now() - lastRx > 90000) {
      s.close();
      return;
    }
    s.send(Uint8Array.of(0xc0, 0));
  }, 20000);

  // 网络恢复 / 页面回到前台：立刻重连，不用等退避计时
  const wake = () => {
    if (closed || ws) return;
    window.clearTimeout(retryTimer);
    open();
  };
  const onVisible = () => {
    if (document.visibilityState === "visible") wake();
  };
  window.addEventListener("online", wake);
  document.addEventListener("visibilitychange", onVisible);

  open();

  return {
    close() {
      closed = true;
      window.clearTimeout(retryTimer);
      window.clearTimeout(connTimer);
      window.clearInterval(pingTimer);
      window.removeEventListener("online", wake);
      document.removeEventListener("visibilitychange", onVisible);
      const s = ws;
      ws = null;
      if (s) {
        try {
          s.close();
        } catch {
          /* ignore */
        }
      }
    },
  };
}
