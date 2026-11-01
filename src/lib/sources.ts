import { buildSourceList } from "./sourceList";

/**
 * 构建时把所有源码文件以文本形式打包进网站，“复制代码”界面才能显示它们。
 * 所有模式都允许“匹配不到文件”，所以即使 firmware/ 文件夹没上传，网站也能正常构建。
 * exhaustive: true 让以点开头的文件（.gitignore、.github/…）也能被匹配到。
 */
const raw = import.meta.glob(
  [
    "/src/**/*.{ts,tsx,css}",
    "/index.html",
    "/package.json",
    "/tsconfig.json",
    "/vite.config.ts",
    "/README.md",
    "/.gitignore",
    "/.github/workflows/*.yml",
    "/firmware/platformio.ini",
    "/firmware/src/*.{h,cpp}",
  ],
  { query: "?raw", import: "default", eager: true, exhaustive: true }
) as Record<string, string>;

export const sourceFiles = buildSourceList(raw);
