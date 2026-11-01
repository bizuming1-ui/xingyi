import { useEffect, useRef, useState } from "react";
import Site from "./Site";
import CodeViewer from "./CodeViewer";
import { copyText } from "../lib/clipboard";
import { normalizeSiteUrl } from "../lib/url";
import { CLEAR_MS, CONFIRM_MS, GITHUB_PAGES_URL, SENSOR_A_PIN, SENSOR_B_PIN } from "../lib/config";
import { initialSensorState, stepSensors, type SensorState } from "../lib/sensorLogic";
import type { SourceFile } from "../lib/sourceList";

interface Props {
  siteUrl: string;
  hasOverride: boolean;
  files: SourceFile[];
  onSaveUrl: (url: string) => void;
  onResetUrl: () => void;
}

/**
 * 模拟两个光电对射传感器（和固件同一套判定）：
 * 点“传感器 A / B”切换遮光状态；A、B 同时遮光 -> “现在”+1，必须都通光后才能再次计数。
 * “模拟物体通过”会自动完成一次：挡 A -> 挡 B（同时遮光）-> 放开 A -> 放开 B。
 */
function VirtualSensors() {
  const [a, setA] = useState(false);
  const [b, setB] = useState(false);
  const [n, setN] = useState(0);
  const st = useRef<SensorState>(initialSensorState());
  const level = useRef({ a: false, b: false });
  const timers = useRef<number[]>([]);

  const apply = (na: boolean, nb: boolean) => {
    level.current = { a: na, b: nb };
    setA(na);
    setB(nb);
  };

  useEffect(() => {
    // 模拟固件检测任务：每 1 毫秒判定一次
    const id = window.setInterval(() => {
      const r = stepSensors(
        st.current,
        level.current.a,
        level.current.b,
        performance.now(),
        CONFIRM_MS,
        CLEAR_MS
      );
      st.current = r.state;
      if (r.counted) {
        setN((x) => x + 1);
        window.dispatchEvent(new CustomEvent("board-press", { detail: { delta: 1 } }));
      }
    }, 1);
    return () => {
      window.clearInterval(id);
      timers.current.forEach((t) => window.clearTimeout(t));
    };
  }, []);

  const pass = () => {
    timers.current.forEach((t) => window.clearTimeout(t));
    const L = level.current;
    const steps: [number, boolean, boolean][] = [
      [0, true, L.b],
      [120, true, true],
      [320, false, true],
      [440, false, false],
    ];
    timers.current = steps.map(([ms, x, y]) => window.setTimeout(() => apply(x, y), ms));
  };

  const cls = (on: boolean) =>
    "select-none rounded-md border px-2.5 py-1.5 text-sm font-semibold " +
    (on
      ? "border-rose-700 bg-rose-600 text-white"
      : "border-emerald-500 bg-emerald-50 text-emerald-800 hover:bg-emerald-100");

  return (
    <div className="flex items-center gap-1.5" title="模拟两个光电对射传感器：A、B 同时遮光 -> 现在 +1">
      <button type="button" className={cls(a)} onClick={() => apply(!level.current.a, level.current.b)}>
        A·GPIO{SENSOR_A_PIN} {a ? "遮光" : "通光"}
      </button>
      <button type="button" className={cls(b)} onClick={() => apply(level.current.a, !level.current.b)}>
        B·GPIO{SENSOR_B_PIN} {b ? "遮光" : "通光"}
      </button>
      <button
        type="button"
        onClick={pass}
        className="rounded-md border border-sky-500 bg-sky-50 px-2.5 py-1.5 text-sm font-semibold text-sky-800 hover:bg-sky-100"
      >
        ▶ 模拟物体通过{n > 0 ? `（已计 ${n}）` : ""}
      </button>
    </div>
  );
}

/** 浏览器模拟器：标签页 + 地址栏 + “复制代码”按钮 + 页面内容 */
export default function Simulator({ siteUrl, hasOverride, files, onSaveUrl, onResetUrl }: Props) {
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

  return (
    <div className="flex h-dvh flex-col p-1 sm:p-3">
      <div className="mx-auto flex min-h-0 w-full max-w-[1500px] flex-1 flex-col overflow-hidden rounded-xl border border-slate-400 bg-white shadow-2xl">
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
          <VirtualSensors />
          <button
            onClick={() => setView(view === "code" ? "site" : "code")}
            className="rounded-md bg-indigo-600 px-4 py-1.5 text-sm font-semibold text-white hover:bg-indigo-500 active:scale-[0.98]"
          >
            {view === "code" ? "返回网站" : "复制代码"}
          </button>
        </div>

        {/* 提示条：正式网址 */}
        <div className="flex flex-wrap items-center gap-x-3 gap-y-1 border-b border-amber-200 bg-amber-50 px-3 py-1 text-xs text-amber-900">
          {tip ? (
            <span>{tip}</span>
          ) : (
            <>
              <span>GitHub 正式网址：</span>
              <a
                href={GITHUB_PAGES_URL}
                target="_blank"
                rel="noreferrer"
                className="font-mono text-indigo-700 underline"
              >
                {GITHUB_PAGES_URL}
              </a>
              <button
                className="rounded bg-amber-200 px-1.5 py-0.5 hover:bg-amber-300"
                onClick={async () =>
                  flash((await copyText(GITHUB_PAGES_URL)) ? "✅ 正式网址已复制" : "❌ 复制失败")
                }
              >
                复制
              </button>
              <span className="text-amber-700">（把“复制代码”里的文件更新到 GitHub 后即可打开）</span>
            </>
          )}
        </div>

        {/* 页面内容：看板固定铺满窗口，不需要滚动 */}
        <div className="relative min-h-0 flex-1 overflow-hidden bg-white">
          {view === "site" ? (
            <div className="stage">
              <div className="stage-fit">
                <Site qrUrl={siteUrl} onCaptionClick={() => setView("code")} />
              </div>
            </div>
          ) : (
            <CodeViewer files={files} />
          )}
        </div>
      </div>
    </div>
  );
}
