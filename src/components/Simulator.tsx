import { useEffect, useState } from "react";
import Site from "./Site";
import CodeViewer from "./CodeViewer";
import { copyText } from "../lib/clipboard";
import { normalizeSiteUrl } from "../lib/url";

interface Props {
  siteUrl: string;
  hasOverride: boolean;
  onSaveUrl: (url: string) => void;
  onResetUrl: () => void;
}

/** 浏览器模拟器：标签页 + 地址栏 + “复制代码”按钮 + 页面内容 */
export default function Simulator({ siteUrl, hasOverride, onSaveUrl, onResetUrl }: Props) {
  const [view, setView] = useState<"site" | "code">("site");
  const [draft, setDraft] = useState(siteUrl);
  const [tip, setTip] = useState("");

  useEffect(() => setDraft(siteUrl), [siteUrl]);

  const flash = (t: string) => {
    setTip(t);
    window.setTimeout(() => setTip(""), 2500);
  };

  const save = () => {
    const n = normalizeSiteUrl(draft);
    if (!n) return flash("❌ 网址格式不正确");
    onSaveUrl(n);
    flash("✅ 网址已保存，二维码已更新");
  };

  const isPublic = hasOverride || /\.github\.io$/i.test(window.location.hostname);

  return (
    <div className="flex h-dvh flex-col p-1 sm:p-4">
      <div className="mx-auto flex min-h-0 w-full max-w-[1400px] flex-1 flex-col overflow-hidden rounded-xl border border-slate-400 bg-white shadow-2xl">
        {/* 标签栏 */}
        <div className="flex items-end gap-2 bg-slate-300 px-3 pt-2">
          <div className="flex items-center gap-2 rounded-t-lg bg-slate-100 px-3 py-1.5 text-sm">
            <span className="flex gap-1">
              <i className="h-2.5 w-2.5 rounded-full bg-red-400" />
              <i className="h-2.5 w-2.5 rounded-full bg-amber-400" />
              <i className="h-2.5 w-2.5 rounded-full bg-green-400" />
            </span>
            <span className="ml-1 max-w-[10rem] truncate">
              {view === "site" ? "生产看板" : "复制代码"}
            </span>
          </div>
        </div>

        {/* 工具栏 */}
        <div className="flex flex-wrap items-center gap-2 border-b border-slate-300 bg-slate-100 px-3 py-2">
          <button
            onClick={() => setView("site")}
            disabled={view === "site"}
            className="h-8 w-8 rounded-full text-lg hover:bg-slate-200 disabled:opacity-30"
            title="返回"
          >
            ←
          </button>
          <button
            onClick={() => window.location.reload()}
            className="h-8 w-8 rounded-full text-lg hover:bg-slate-200"
            title="刷新"
          >
            ⟳
          </button>

          <div className="flex min-w-[12rem] flex-1 items-center gap-1 rounded-full border border-slate-300 bg-white px-3 py-1">
            <span className="text-xs text-slate-500">🔒</span>
            <input
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && save()}
              spellCheck={false}
              className="min-w-0 flex-1 bg-transparent text-sm outline-none"
              aria-label="网址"
            />
            <button
              onClick={save}
              className="rounded-full bg-slate-200 px-2 py-0.5 text-xs hover:bg-slate-300"
              title="保存为二维码网址"
            >
              保存
            </button>
            {hasOverride && (
              <button
                onClick={onResetUrl}
                className="rounded-full bg-slate-200 px-2 py-0.5 text-xs hover:bg-slate-300"
                title="恢复默认网址"
              >
                重置
              </button>
            )}
          </div>

          <a
            href={siteUrl}
            target="_blank"
            rel="noreferrer"
            className="rounded-md border border-slate-300 bg-white px-3 py-1.5 text-sm hover:bg-slate-50"
          >
            新窗口打开 ↗
          </a>
          <button
            onClick={async () =>
              flash((await copyText(siteUrl)) ? "✅ 网址已复制" : "❌ 复制失败，请手动复制地址栏")
            }
            className="rounded-md border border-slate-300 bg-white px-3 py-1.5 text-sm hover:bg-slate-50"
          >
            复制网址
          </button>
          <button
            onClick={() => setView(view === "code" ? "site" : "code")}
            className="rounded-md bg-indigo-600 px-4 py-1.5 text-sm font-semibold text-white hover:bg-indigo-500 active:scale-[0.98]"
          >
            {view === "code" ? "返回网站" : "复制代码"}
          </button>
        </div>

        {(tip || !isPublic) && (
          <div className="border-b border-amber-200 bg-amber-50 px-3 py-1.5 text-xs text-amber-800">
            {tip ||
              "提示：当前网址是预览/本地地址，手机可能打不开。把代码上传到 GitHub Pages 后，将线上网址粘贴到上方地址栏并点“保存”，二维码就会指向线上网站（步骤见“复制代码”里的 README.md）。"}
          </div>
        )}

        {/* 页面内容 */}
        <div className="min-h-0 flex-1 overflow-auto bg-white">
          {view === "site" ? (
            <div className="mx-auto max-w-[1340px]">
              <Site qrUrl={siteUrl} onCaptionClick={() => setView("code")} />
            </div>
          ) : (
            <CodeViewer />
          )}
        </div>
      </div>
    </div>
  );
}
