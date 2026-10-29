// Arena previews are only accessible within Arena, not to a phone scanning a QR code.
// A configured public URL can also be used for other hosting environments.
export function publicAppUrl(location, basePath, configuredUrl) {
  if (configuredUrl) return new URL(configuredUrl).href;
  const host = location.hostname.toLowerCase();
  if (host.endsWith('.arena.site') || host.endsWith('.e2b.app')) {
    return 'https://bizuming1-ui.github.io/xingyi/';
  }
  return new URL(basePath, location.origin).href;
}
