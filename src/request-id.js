// Secure, standards-compliant UUIDs also work on browsers without randomUUID.
export function requestId(cryptoObject = globalThis.crypto) {
  if (typeof cryptoObject?.randomUUID === 'function') return cryptoObject.randomUUID();
  if (typeof cryptoObject?.getRandomValues !== 'function') throw new Error('浏览器不支持安全随机数，请使用最新版浏览器并通过 HTTPS 访问。');
  const b = cryptoObject.getRandomValues(new Uint8Array(16));
  b[6] = (b[6] & 15) | 64;
  b[8] = (b[8] & 63) | 128;
  const h = Array.from(b, n => n.toString(16).padStart(2, '0')).join('');
  return `${h.slice(0,8)}-${h.slice(8,12)}-${h.slice(12,16)}-${h.slice(16,20)}-${h.slice(20)}`;
}
