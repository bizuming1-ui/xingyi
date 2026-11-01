import { useEffect, useState } from "react";
import Site from "./components/Site";
import Simulator from "./components/Simulator";
import { defaultSiteUrl, loadOverride, saveOverride } from "./lib/url";

function useHash() {
  const [hash, setHash] = useState(window.location.hash);
  useEffect(() => {
    const on = () => setHash(window.location.hash);
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);
  return hash;
}

export default function App() {
  const hash = useHash();
  const [override, setOverride] = useState(loadOverride);
  const siteUrl = override || defaultSiteUrl();

  // #/site  -> 纯网站页面（手机扫码打开这里）
  // 其它    -> 浏览器模拟器（含“复制代码”按钮）
  if (hash.startsWith("#/site")) {
    return (
      <div className="mx-auto min-h-dvh max-w-[1340px] bg-white">
        <Site qrUrl={siteUrl} />
      </div>
    );
  }

  return (
    <Simulator
      siteUrl={siteUrl}
      hasOverride={!!override}
      onSaveUrl={(u) => {
        saveOverride(u);
        setOverride(u);
      }}
      onResetUrl={() => {
        saveOverride("");
        setOverride("");
      }}
    />
  );
}
