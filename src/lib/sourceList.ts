/** 把“路径 -> 文件内容”整理成“复制代码”界面使用的分类文件列表（Vite 网站和预览环境共用） */

export type Group = "github" | "platformio";
export type Status = "new" | "changed" | "same";

export interface SourceFile {
  group: Group;
  /** 界面显示的路径（PlatformIO 文件显示的是 PlatformIO 项目内的路径） */
  path: string;
  /** 在 GitHub 仓库里的路径 */
  repoPath: string;
  content: string;
  /** new=新增文件；changed=仓库里已有、需要用新内容覆盖；same=和仓库里完全一致，无需改动 */
  status: Status;
}

/** 你的 GitHub 仓库里已经存在的文件（用于判断是“新增”还是“覆盖”） */
const EXISTING = new Set([
  ".github/workflows/deploy.yml",
  ".gitignore",
  "README.md",
  "index.html",
  "package.json",
  "platformio/platformio.ini",
  "platformio/src/config.h",
  "platformio/src/main.cpp",
  "src/App.tsx",
  "src/board.css",
  "src/components/CodeViewer.tsx",
  "src/components/Simulator.tsx",
  "src/components/Site.tsx",
  "src/index.css",
  "src/lib/board.ts",
  "src/lib/clipboard.ts",
  "src/lib/config.ts",
  "src/lib/mqttLite.ts",
  "src/lib/sourceList.ts",
  "src/lib/sources.ts",
  "src/lib/url.ts",
  "src/lib/useDeviceLink.ts",
  "src/main.tsx",
  "src/utils/cn.ts",
  "src/vite-env.d.ts",
  "tsconfig.json",
  "vite.config.ts",
]);

/** 已有文件中，内容和本次开发完全一致的（无需再复制） */
const SAME = new Set<string>([
  ".github/workflows/deploy.yml",
  ".gitignore",
  "index.html",
  "package.json",
  "src/App.tsx",
  "src/board.css",
  "src/index.css",
  "src/lib/clipboard.ts",
  "src/lib/mqttLite.ts",
  "src/lib/url.ts",
  "src/main.tsx",
  "src/utils/cn.ts",
  "src/vite-env.d.ts",
  "tsconfig.json",
  "vite.config.ts",
]);

const ORDER_GITHUB = [
  "README.md",
  ".github/workflows/deploy.yml",
  "package.json",
  "index.html",
  "vite.config.ts",
  "tsconfig.json",
  ".gitignore",
  "src/main.tsx",
  "src/App.tsx",
  "src/index.css",
  "src/board.css",
  "src/vite-env.d.ts",
  "src/components/Simulator.tsx",
  "src/components/Site.tsx",
  "src/components/CodeViewer.tsx",
  "src/lib/config.ts",
  "src/lib/board.ts",
  "src/lib/mqttLite.ts",
  "src/lib/useDeviceLink.ts",
  "src/lib/sensorLogic.ts",
  "src/lib/sourceList.ts",
  "src/lib/sources.ts",
  "src/lib/url.ts",
  "src/lib/clipboard.ts",
  "src/utils/cn.ts",
];

const ORDER_PIO = ["platformio.ini", "src/config.h", "src/main.cpp"];

export function buildSourceList(raw: Record<string, string>): SourceFile[] {
  const out: SourceFile[] = [];
  for (const [key, text] of Object.entries(raw)) {
    const repoPath = key.replace(/^\/+/, "");
    const content = text.replace(/\r\n/g, "\n");
    if (repoPath.startsWith("platformio/")) {
      out.push({
        group: "platformio",
        path: repoPath.slice("platformio/".length),
        repoPath,
        content,
        status: SAME.has(repoPath) ? "same" : EXISTING.has(repoPath) ? "changed" : "new",
      });
    } else {
      out.push({
        group: "github",
        path: repoPath,
        repoPath,
        content,
        status: SAME.has(repoPath) ? "same" : EXISTING.has(repoPath) ? "changed" : "new",
      });
    }
  }
  const rank = (f: SourceFile) => {
    const list = f.group === "github" ? ORDER_GITHUB : ORDER_PIO;
    const i = list.indexOf(f.path);
    return i === -1 ? 999 : i;
  };
  return out.sort((a, b) =>
    a.group !== b.group
      ? a.group === "github"
        ? -1
        : 1
      : rank(a) - rank(b) || a.path.localeCompare(b.path)
  );
}
