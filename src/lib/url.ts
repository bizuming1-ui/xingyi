const KEY = "site-url-override";

/** 当前页面对应的“纯网站页面”地址（二维码默认指向这里） */
export function defaultSiteUrl(): string {
  try {
    const u = new URL(window.location.href);
    u.search = "";
    u.hash = "#/site";
    return u.toString();
  } catch {
    return "";
  }
}

/** 规范化用户输入的网址：补全 https://，并确保指向 #/site 页面 */
export function normalizeSiteUrl(raw: string): string {
  let s = raw.trim();
  if (!s) return "";
  if (!/^https?:\/\//i.test(s)) s = "https://" + s;
  try {
    const u = new URL(s);
    if (!u.hash.startsWith("#/site")) u.hash = "#/site";
    return u.toString();
  } catch {
    return "";
  }
}

export function loadOverride(): string {
  try {
    return localStorage.getItem(KEY) || "";
  } catch {
    return "";
  }
}

export function saveOverride(value: string) {
  try {
    if (value) localStorage.setItem(KEY, value);
    else localStorage.removeItem(KEY);
  } catch {
    /* ignore */
  }
}
