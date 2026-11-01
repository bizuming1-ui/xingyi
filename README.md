# 生产看板（含浏览器模拟器 + 复制代码界面）

## 功能
- 页面版式与参考图一致：生产单、产品、生产总数、当前生产数、现在、确认、右上角二维码。
- 生产单 / 产品 / 生产总数 / 现在：可手动输入。
- 当前生产数：**不可手动填写**。点击“确认”后，“现在”里的数字累加到“当前生产数”，然后“现在”清零。
- 二维码：编码的是网站地址（`…/#/site`），手机扫码即可打开纯网站页面。
- 浏览器模拟器：带地址栏（可填写你的 GitHub Pages 网址，二维码会同步更新）、“复制代码”按钮，
  进入后可查看并一键复制所有代码文件。

## 本地运行
```bash
npm install
npm run dev      # 开发
npm run build    # 构建，产物是 dist/index.html（单文件）
```

## 发布到 GitHub 并得到可直接打开的网址（GitHub Pages）
1. 在 GitHub 新建一个空仓库，例如 `production-board`（不要勾选 README）。
2. 在项目目录执行（把 `你的用户名` 换成你的 GitHub 用户名）：
```bash
git init
git add .
git commit -m "init production board"
git branch -M main
git remote add origin https://github.com/你的用户名/production-board.git
git push -u origin main
```
3. 打开仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**。
4. 到 **Actions** 页面等待 “Deploy to GitHub Pages” 变成绿色（约 1 分钟）。
5. 网址就是：
   - 完整页面（含浏览器模拟器）：`https://你的用户名.github.io/production-board/`
   - 纯网站页面（二维码指向这里）：`https://你的用户名.github.io/production-board/#/site`
6. 回到浏览器模拟器地址栏，粘贴上面的网址并回车/点击“保存”，二维码就会变成线上地址，手机扫码即可打开。
