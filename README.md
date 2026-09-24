# GerritPilot Next

使用 Qt 6.8.3、QML 和 C++ 开发的 `repo + Gerrit` 多仓代码管理工具。界面采用受 iOS/macOS 启发的设计语言：低噪声色彩、分层卡片、圆角操作控件、侧栏导航和明确的确认流程。

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
├── qml/
│   ├── App.qml
│   ├── Theme.qml
│   ├── pages/
│   │   └── WorkspacePage.qml
│   ├── features/
│   │   ├── navigation/        # 侧边栏与仓库/分支导航
│   │   ├── repositories/      # 详情面板、Diff 查看、提交编辑与历史
│   │   └── overlays/          # 弹窗与设置抽屉面板
│   └── components/            # 通用基础组件（按钮、卡片、滚动条等）
├── src/
│   ├── main.cpp
│   ├── workspacecontroller.*  # 应用协调控制器
│   ├── processrunner.*        # 低层进程执行器
│   ├── repositorymodel.*      # 仓库列表模型
│   ├── repositoryproxymodel.* # 仓库过滤与搜索代理模型
│   ├── gitstatusparser.h      # NUL 分隔状态解析
│   ├── githistoryparser.h     # 提交历史结构化解析
│   ├── gitqueries.h           # 纯 Git 命令参数定义
│   ├── repositorycache.h      # 仓库状态快照与磁盘缓存
│   └── discardplan.h          # 丢弃改动安全规划
└── tests/                     # CTest C++ 单元测试与 QML 界面测试
```

## 设计原则

1. 批量操作以“勾选仓库”为上下文，单仓操作以“当前仓库”为上下文。
2. 分支切换、新建分支、同步、提交和上传等写操作在执行前展示目标或实际命令。
3. 状态颜色只用于表达含义：绿色为干净、橙色为本地改动、紫色为待上传、红色为错误。
4. Qt 6 仅在项目 Kit 中显式启用，不覆盖任何已有 Qt 环境。
