import { useState } from "react";
import { copyText } from "../lib/clipboard";
import { GITHUB_REPO } from "../lib/config";
import type { Group, SourceFile } from "../lib/sourceList";

const TABS: { id: Group; title: string; hint: string[] }[] = [
  {
    id: "github",
    title: "① 更新到 GitHub 的代码",
    hint: [
      "标“新增”的文件：在仓库点 Add file → Create new file，文件名填左侧路径，粘贴代码，Commit。",
      "标“覆盖”的文件：点右侧“在 GitHub 编辑”，全选后粘贴，Commit。标“无需修改”的跳过。",
      "全部提交后，Actions 变绿（约 1 分钟），网站就更新了。Settings → Pages → Source 选 GitHub Actions。",
    ],
  },
  {
    id: "platformio",
    title: "② 复制到 VS Code PlatformIO 的代码",
    hint: [
      "VS Code → PlatformIO Home → New Project：Board 选 Espressif ESP32-S3-DevKitC-1，Framework 选 Arduino。",
      "用下面 3 个文件覆盖/新建：platformio.ini、src/config.h（先改 WiFi 名称和密码）、src/main.cpp。",
      "按键一端接 GND，另一端接 GPIO4。点 PlatformIO 的 Upload 上传，Monitor 查看串口日志。",
    ],
  },
];

const STATUS_LABEL = { new: "新增", changed: "覆盖", same: "无需修改" } as const;
const STATUS_CLS = {
  new: "bg-emerald-500/20 text-emerald-300",
  changed: "bg-amber-500/20 text-amber-300",
  same: "bg-slate-500/30 text-slate-300",
} as const;

function githubLink(f: SourceFile): { href: string; label: string } {
  const base = `https://github.com/${GITHUB_REPO}`;
  return f.status === "new"
    ? { href: `${base}/new/main?filename=${encodeURI(f.repoPath)}`, label: "在 GitHub 新建" }
    : { href: `${base}/edit/main/${encodeURI(f.repoPath)}`, label: "在 GitHub 编辑" };
}

export default function CodeViewer({ files }: { files: SourceFile[] }) {
  const [tab, setTab] = useState<Group>("github");
  const [picked, setPicked] = useState<Record<Group, string>>({ github: "", platformio: "" });
  const [tip, setTip] = useState("");

  const list = files.filter((f) => f.group === tab);
  const file = list.find((f) => f.path === picked[tab]) ?? list[0];
  const info = TABS.find((t) => t.id === tab)!;

  const flash = (t: string) => {
    setTip(t);
    window.setTimeout(() => setTip(""), 2500);
  };

  const copy = async (text: string, label: string) => {
    const ok = await copyText(text);
    flash(ok ? `✅ 已复制：${label}` : "❌ 自动复制失败，请在代码框里全选后手动复制");
  };

  const allText = () =>
    list.map((f) => `===== 文件：${f.path} =====\n${f.content}`).join("\n\n");

  return (
    <div className="absolute inset-0 flex flex-col bg-slate-900 text-slate-100">
      {/* 分类标签 */}
      <div className="flex shrink-0 gap-2 border-b border-slate-700 bg-slate-950 px-3 pt-2">
        {TABS.map((t) => (
          <button
            key={t.id}
            onClick={() => setTab(t.id)}
            className={
              "rounded-t-md px-4 py-2 text-sm font-semibold " +
              (tab === t.id
                ? "bg-slate-900 text-white"
                : "bg-slate-800 text-slate-400 hover:text-slate-200")
            }
          >
            {t.title}（{files.filter((f) => f.group === t.id).length}）
          </button>
        ))}
      </div>

      <ol className="shrink-0 list-decimal space-y-0.5 border-b border-slate-700 bg-slate-800/60 py-2 pl-8 pr-3 text-xs text-slate-300">
        {info.hint.map((h) => (
          <li key={h}>{h}</li>
        ))}
      </ol>

      {list.length === 0 || !file ? (
        <div className="p-6 text-sm text-slate-400">
          没有找到这一类的文件（如果是 PlatformIO：请把仓库里的 firmware/ 文件夹也一起上传）。
        </div>
      ) : (
        <div className="flex min-h-0 flex-1 flex-col md:flex-row">
          <aside className="max-h-36 shrink-0 overflow-auto border-b border-slate-700 md:max-h-none md:w-72 md:border-b-0 md:border-r">
            <div className="sticky top-0 z-10 bg-slate-900 p-2">
              <button
                onClick={() => copy(allText(), `${info.title} 全部`)}
                className="w-full rounded-md bg-emerald-500 px-3 py-2 text-sm font-semibold text-slate-950 hover:bg-emerald-400 active:scale-[0.98]"
              >
                复制本类全部代码
              </button>
            </div>
            <ul className="px-2 pb-3">
              {list.map((f) => (
                <li key={f.path}>
                  <button
                    onClick={() => setPicked((p) => ({ ...p, [tab]: f.path }))}
                    className={
                      "flex w-full items-center gap-2 rounded px-2 py-1.5 text-left font-mono text-xs " +
                      (f.path === file.path
                        ? "bg-slate-700 text-white"
                        : "text-slate-300 hover:bg-slate-800")
                    }
                    title={f.path}
                  >
                    <span className="min-w-0 flex-1 truncate">{f.path}</span>
                    <span className={"shrink-0 rounded px-1 py-0.5 text-[10px] " + STATUS_CLS[f.status]}>
                      {STATUS_LABEL[f.status]}
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          </aside>

          <section className="flex min-h-0 min-w-0 flex-1 flex-col">
            <div className="flex flex-wrap items-center gap-2 border-b border-slate-700 bg-slate-800 px-3 py-2">
              <span className="min-w-0 flex-1 truncate font-mono text-sm">{file.path}</span>
              <span className="text-xs text-slate-400">{file.content.split("\n").length} 行</span>
              <button
                onClick={() => copy(file.path, `文件名 ${file.path}`)}
                className="rounded-md bg-slate-600 px-2.5 py-1.5 text-xs font-semibold hover:bg-slate-500"
              >
                复制文件名
              </button>
              <a
                href={githubLink(file).href}
                target="_blank"
                rel="noreferrer"
                className="rounded-md bg-slate-600 px-2.5 py-1.5 text-xs font-semibold hover:bg-slate-500"
                title={tab === "platformio" ? "可选：把固件也存进仓库" : undefined}
              >
                {githubLink(file).label} ↗
              </a>
              <button
                onClick={() => copy(file.content, file.path)}
                className="rounded-md bg-sky-500 px-3 py-1.5 text-sm font-semibold text-slate-950 hover:bg-sky-400 active:scale-[0.98]"
              >
                复制此文件
              </button>
            </div>
            {tip && <div className="bg-slate-950 px-3 py-1.5 text-sm text-amber-300">{tip}</div>}
            <pre className="min-h-0 flex-1 select-text overflow-auto p-3 font-mono text-xs leading-relaxed whitespace-pre text-slate-200">
              {file.content}
            </pre>
          </section>
        </div>
      )}
    </div>
  );
}
