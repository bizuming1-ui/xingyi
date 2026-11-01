import { useEffect, useState } from "react";
import Site from "./components/Site";
import Simulator from "./components/Simulator";
import { defaultSiteUrl, loadOverride, saveOverride } from "./lib/url";
import type { SourceFile } from "./lib/sourceList";

function useHash() {
  const [hash, setHash] = useState(window.location.hash);
  useEffect(() => {
    const on = () => setHash(window.location.hash);
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);
  return hash;
}

export default function App({ files }: { files: SourceFile[] }) {
  const hash = useHash();
  const [override, setOverride] = useState(loadOverride);
  const siteUrl = override || defaultSiteUrl();

  // #/site  -> 纯网站页面（手机扫码打开这里），铺满整个窗口，不需要滚动
  // 其它    -> 浏览器模拟器（含“复制代码”按钮）
  if (hash.startsWith("#/site")) {
    return (
      <div className="relative h-dvh w-full bg-white">
        <div className="stage">
          <div className="stage-fit">
            <Site qrUrl={siteUrl} />
          </div>
        </div>
      </div>
    );
  }

  return (
    <Simulator
      siteUrl={siteUrl}
      hasOverride={!!override}
      files={files}
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
