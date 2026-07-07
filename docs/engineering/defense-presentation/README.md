# ebbackup 答辩演讲稿（双 PDF）

与 [`ecosystem-evolution`](../ecosystem-evolution/)（为何改）、[`kernel-manual`](../kernel-manual/)（是什么）互补，本目录产出**答辩口播材料**。

## 产物

| 文件 | 说明 |
|------|------|
| `build/slides.pdf` | Beamer 16:9 投屏幻灯片（含 `\note{}` 提要） |
| `build/script.pdf` | 逐字演讲稿：Part I 课程 8–10 min + Part II 技术 15–20 min + Q&A |

## 构建

```bash
cd docs/engineering/defense-presentation
make all      # 或 make slides / make script
```

需 **XeLaTeX** + `ctex` 宏包。正文字体复用 Workbench [`gui/public/fonts/`](../../../gui/public/fonts/)（JetBrains Mono NL + ProggyClean 代码块）。

### Windows（推荐 WSL）

本机 PowerShell 若无 `xelatex`，可在 WSL Debian/Ubuntu 中构建：

```bash
cd /mnt/e/recoveryProjects/docs/engineering/defense-presentation
make all      # 或 make slides / make script
```

若 WSL 尚未安装构建工具，在 WSL 终端执行（需输入密码）：

```bash
sudo apt update
sudo apt install -y make texlive-xetex texlive-lang-chinese texlive-latex-extra
```

### Linux / macOS

```bash
cd docs/engineering/defense-presentation
make all
```

- **课程答辩 / 3 分钟演示视频**：只讲 Part I，按 `\timing` 左侧「课程版」控制节奏。
- **技术深度答辩**：Part I + Part II，按 `\timing` 右侧「技术版」。

## 与课程提交物对照

| 课程要求 | 本目录 / 仓库对应 |
|----------|-------------------|
| 需求分析（用例图） | Part I 功能对照表；可另导 StarUML 用例图 |
| 系统设计（类/顺序/构件图） | `chapters/figures-*.tex`；详见 kernel-manual |
| 测试报告 | Part I 测试节 + `ci_floor.json` 节选 |
| 源码+构建脚本 | 仓库根 CMake + `gui/scripts/build_desktop.ps1` |
| PPT | `slides.pdf` 可代替或补充 |
| 演示视频 | 按 Part I「演示脚本」节录屏 |

## 截图（可选）

将 Workbench 截图放入 `figures/`，在 `main-script.tex` 中 `\includegraphics` 即可；初版使用文字演示提示，不依赖截图亦可编译。
