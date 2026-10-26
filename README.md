# GerritPilot Next

使用 Qt 6.8.3、QML 和 C++ 开发的 `repo + Gerrit` 多仓代码管理工具。界面采用受 iOS/macOS 启发的设计语言：低噪声色彩、分层卡片、圆角操作控件、侧栏导航和明确的确认流程。

> 完整项目文档已统一沉淀至项目知识库：**[code/Readme/GerritPilot](/home/liushuai/code/Readme/GerritPilot/README.md)**
> - [功能与操作指南](/home/liushuai/code/Readme/GerritPilot/功能与操作指南.md)
> - [系统架构与核心设计](/home/liushuai/code/Readme/GerritPilot/系统架构与核心设计.md)
> - [构建部署与验收规范](/home/liushuai/code/Readme/GerritPilot/构建部署与验收规范.md)

## 当前能力

- 读取 `.repo/project.list` 或单 Git 仓库
- 异步检查所有仓库的分支、改动数和 ahead/behind 状态
- 查看按最近提交排序的本地分支，并安全切换或基于当前 HEAD 新建分支
- 日常拉取始终使用各 Git 分支的上游（仅快进，不自动 rebase/stash）；不会因存在 `.repo` 而改用 repo sync
- 设置中的“添加项目 · Repo 初始化”支持导入本地 Git/repo 项目，以及在空目录 init → 检查 Manifest → 全量 sync
- 查看当前仓库的改动与支持分页及搜索的结构化提交历史
- 暂存改动、创建提交或 amend
- 推送 `HEAD:refs/for/<branch>`，支持 Gerrit Topic
- 从 Gerrit 安装 `commit-msg` Change-Id Hook
- 所有进程调用均通过 `QProcess(program, arguments)`，不保存 SSH 密码、不拼接 Shell
- 工作区和 Gerrit 配置通过 `QSettings` 持久化，支持按仓库草稿与配置隔离

## Qt 环境隔离与验证

本工程固定使用独立安装的 Qt 6.8.3，不修改系统默认 Qt 5.6.3：

```text
/home/liushuai/Qt6.8.3/6.8.3/gcc_64
```

在 Qt Creator 中配置 Kit 时，选择：

```text
CMake: /home/liushuai/Qt6.8.3/6.8.3/gcc_64/bin/qt-cmake
qmake: /home/liushuai/Qt6.8.3/6.8.3/gcc_64/bin/qmake6
```

命令行构建与测试：

```bash
cmake --build /home/liushuai/code/build-GerritPilotNext-Qt6-Debug --parallel 4
ctest --test-dir /home/liushuai/code/build-GerritPilotNext-Qt6-Debug --output-on-failure
env QT_QPA_PLATFORM=offscreen QT_QUICK_CONTROLS_STYLE=Basic QML_DISABLE_DISK_CACHE=1 \
  /home/liushuai/Qt6.8.3/6.8.3/gcc_64/bin/qmltestrunner \
  -import /home/liushuai/code/GerritPilotNext/tests/imports \
  -input /home/liushuai/code/GerritPilotNext/tests
```

## 工程结构

```text
GerritPilotNext/
├── CMakeLists.txt
├── packaging/gerritpilot.desktop  # Linux 桌面入口
├── qml/
│   ├── App.qml
│   ├── Theme.qml                  # 设计 Token：iOS 系统色、填充、语义色调、Diff 配色、圆角/间距/动效
│   ├── assets/gerritpilot.svg     # 应用 Logo（窗口图标、侧栏品牌、桌面图标共用）
│   ├── pages/WorkspacePage.qml
│   ├── features/
│   │   ├── navigation/            # Sidebar = ProjectSwitcher + RepositoryList/Row + BranchList + WorkspaceStatusCard
│   │   ├── repositories/          # RepositoryDetailPane = RepositoryToolbar + OperationBanner + DiffToolbar
│   │   │                          #   + ChangesFileList/CommitEditor/HistoryPane + CommandLogPopup + ConfirmDialogs
│   │   └── overlays/              # 推送/拉取/分支/设置/错误弹窗
│   └── components/                # 设计系统组件（见下）
├── src/                           # C++ 控制器、Git/Gerrit 服务与解析器
└── qa/                            # QML 视觉测试与源码约束检查
```

### 设计系统组件（qml/components）

| 组件 | 用途 |
|------|------|
| `Icon` + `Icons.js` | 矢量线性图标（24 栅格，圆角端点，参照 Lucide/ISC），替代 Unicode 符号 |
| `PrimaryButton` | `kind`: filled / gray / tinted / plain / danger / dangerTinted，`compact` 小尺寸 |
| `IconButton`、`FileActionButton`、`ToggleChip` | 工具栏图标按钮、行内操作、可勾选胶囊 |
| `SegmentedControl` | iOS 分段控件（主视图切换、统一/左右 Diff） |
| `SearchField` | iOS 搜索框（放大镜 + 清除按钮 + 尾部插槽） |
| `Badge`、`Avatar`、`Spinner` | 胶囊徽标、作者头像、iOS 风格加载指示 |
| `Banner`、`SectionHeader`、`SettingsGroup` | 语义提示条、分组标题、iOS 分组设置卡片 |
| `AppDialog`、`FloatingSurface`、`AppMenu(Item)` | 弹窗/浮层（软阴影，软件渲染下同样可用）、菜单 |
| `AppTextField`、`AppComboBox`、`AppRadioButton`、`AppSpinBox` | 统一样式的表单控件 |

新增界面时优先组合以上组件，颜色只引用 `Theme` Token，不在页面中硬编码色值。

## 设计原则

1. 批量操作以“勾选仓库”为上下文，单仓操作以“当前仓库”为上下文。
2. 分支切换、新建分支、同步、提交和上传等写操作在执行前展示目标或实际命令。
3. 状态颜色只用于表达含义：绿色为干净、橙色为本地改动、紫色为待上传、红色为错误。
4. Qt 6 仅在项目 Kit 中显式启用，不覆盖任何已有 Qt 环境。
