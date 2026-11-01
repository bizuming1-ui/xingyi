import { useState } from "react";
import { allAsText, sourceFiles } from "../lib/sources";
import { copyText } from "../lib/clipboard";

export default function CodeViewer() {
  const [selected, setSelected] = useState(sourceFiles[0].path);
  const [tip, setTip] = useState("");
  const file = sourceFiles.find((f) => f.path === selected) ?? sourceFiles[0];

  const flash = (t: string) => {
    setTip(t);
    window.setTimeout(() => setTip(""), 2500);
  };

  const copy = async (text: string, label: string) => {
    const ok = await copyText(text);
    flash(ok ? `✅ 已复制：${label}` : "❌ 自动复制失败，请在代码框里长按/全选后手动复制");
  };

  return (
    <div className="flex h-full min-h-0 flex-col bg-slate-900 text-slate-100 md:flex-row">
      <aside className="max-h-44 shrink-0 overflow-auto border-b border-slate-700 md:max-h-none md:w-72 md:border-b-0 md:border-r">
        <div className="sticky top-0 z-10 space-y-2 bg-slate-900 p-3">
          <div className="text-sm font-semibold">代码文件（共 {sourceFiles.length} 个）</div>
          <button
            onClick={() => copy(allAsText(), "全部代码")}
            className="w-full rounded-md bg-emerald-500 px-3 py-2 text-sm font-semibold text-slate-950 hover:bg-emerald-400 active:scale-[0.98]"
          >
            复制全部代码
          </button>
        </div>
        <ul className="px-2 pb-3">
          {sourceFiles.map((f) => (
            <li key={f.path}>
              <button
                onClick={() => setSelected(f.path)}
                className={
                  "w-full truncate rounded px-2 py-1.5 text-left font-mono text-xs " +
                  (f.path === file.path
                    ? "bg-slate-700 text-white"
                    : "text-slate-300 hover:bg-slate-800")
                }
                title={f.path}
              >
                {f.path}
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
            onClick={() => copy(file.content, file.path)}
            className="rounded-md bg-sky-500 px-3 py-1.5 text-sm font-semibold text-slate-950 hover:bg-sky-400 active:scale-[0.98]"
          >
            复制此文件
          </button>
        </div>
        {tip && (
          <div className="bg-slate-950 px-3 py-1.5 text-sm text-amber-300">{tip}</div>
        )}
        <pre className="min-h-0 flex-1 select-text overflow-auto p-3 font-mono text-xs leading-relaxed whitespace-pre text-slate-200">
          {file.content}
        </pre>
      </section>
    </div>
  );
}
