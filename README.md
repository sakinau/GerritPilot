# GerritPilot

GerritPilot 是一款面向 Git、Repo 和 Gerrit 工作流的 Linux 桌面工具。它将多仓库状态、代码改动、提交和 Gerrit 评审操作集中在一个界面中，也支持打开普通的单 Git 仓库。

> 项目目前处于早期试用阶段。真实 Gerrit 服务、AI 接口和不同 Linux 环境的兼容性仍在完善；建议先在测试仓库中熟悉操作。

## 主要功能

- **项目与仓库**：导入本地 Git 或 Repo 项目，查看仓库状态、分支和提交历史；支持初始化新的 Repo 项目。
- **代码改动**：查看文件 Diff，按文件或改动块暂存，填写提交说明并创建或修改本地提交。
- **同步代码**：按分支上游执行 Git 拉取，也可对 Repo 项目按 Manifest 同步；支持处理变基和摘取冲突。
- **Gerrit 评审**：推送单个或多个仓库，设置目标分支、Topic 和评审人；下载 Change，检查及安装 Change-Id Hook。
- **AI 提交说明**：使用兼容 Chat Completions 的接口，根据选定改动生成提交说明，生成后仍可人工检查和编辑。

## 环境要求

- Linux x86-64 图形桌面。当前试用包尚未在无 Qt SDK 的干净环境完成验证。
- 已安装 Git；使用 Repo 多仓项目时还需要 `repo` 命令。
- 访问远程仓库或 Gerrit 时，需预先配置可用的网络、SSH 密钥或 HTTPS 凭据，以及相应的仓库权限。

## 启动

### 使用试用包

获取内部试用压缩包后，在终端进入压缩包所在目录：

```bash
tar -xzf GerritPilot-v0.1.0-beta.1-linux-x86_64-internal-20261028.tar.gz
cd GerritPilot-v0.1.0-beta.1-linux-x86_64-internal-20261028
./bin/gerritpilot
```

该包当前用于内部试用，尚未作为 GitHub Release 发布；后续版本的文件名可能不同。

### 从源码构建

需要 C++17 编译器、CMake 3.21 或更新版本，以及包含 Quick、Quick Controls 2、Network 和 SVG 的 Qt 6.8 开发环境。以下命令由使用者在项目根目录执行，将 Qt 路径替换为本机的实际安装位置：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build --target gerritpilot -j4
./build/gerritpilot
```

如果系统已能找到合适的 Qt，可以省略 `CMAKE_PREFIX_PATH`。安装到指定目录可使用 `cmake --install build --prefix /path/to/install`。

## 快速上手

1. 启动后，在左上角项目切换器选择 **添加项目**，选择包含 `.git` 的 Git 仓库或包含 `.repo` 的 Repo 项目根目录。导入已有项目不会自动联网。
2. 在左侧选择仓库，查看当前分支和改动；在 **工作区改动** 中查看 Diff，并暂存需要提交的文件或改动块。
3. 在 **提交说明** 中填写内容，点击 **提交** 创建本地提交。需要更新最近一次提交时，可选择 **修改上次提交**；此操作会改写该提交。
4. 使用 **拉取代码** 更新分支。Git 拉取使用当前分支配置的上游；Repo 同步按 Manifest 工作，两者的目标可能不同。
5. 需要代码评审时，点击 **推送**，确认远端、Gerrit 目标分支和其他选项后推送。推送前会检查待上传提交的 Change-Id；多个仓库可从仓库列表选择批量推送。

新建 Repo 项目时，在项目管理中填写空目录和 Manifest 信息并执行初始化；初始化后按需检查 Manifest，再手动选择 Repo 同步整个项目。

## AI 生成提交说明

在 **设置 → AI 提交说明** 中填写接口 Base URL（或完整的 `/chat/completions` 地址）、模型名称和 API Key，然后点击 **测试连接**。也可以通过环境变量 `GERRITPILOT_AI_API_KEY` 提供密钥；选择“记住 API Key”时，密钥会明文保存在本机设置中。

在提交面板点击 **AI 生成**，选择改动范围、提交类型并填写关联单号。当前生成规则适配 smart-commit v2.0：单号为非全零的 10 位数字，修复提交需要同时填写问题单和需求单。仅在确实完成编译验证后勾选相应选项。

生成时，所选范围的文件信息和 Diff 片段会发送给配置的 AI 接口；请确认代码适合发送。**测试连接不会上传代码。**生成结果应在提交前人工核对。

## 常见问题

- **拉取提示没有上游或无法快进**：检查当前分支的上游；如果有本地提交，可在确认工作区状态后选择变基拉取。遇到冲突时先解决冲突，再在界面中继续或终止。
- **Gerrit 推送提示缺少 Change-Id**：按界面提示检查或安装 `commit-msg` Hook。程序可在确认后修订最近一次提交以补充 Change-Id；较早的提交需要逐一处理。
- **SSH 或 HTTPS 操作失败**：先在终端检查远端地址、网络和认证。SSH 可检查 `~/.ssh/config`；HTTPS 可先在终端配置 Git 凭据并执行一次 `git fetch`。
- **AI 无法生成**：检查接口地址、模型、密钥和网络；确认已选择有改动的范围，并填写符合规则的关联单号。

## 许可证

本项目源码采用 [MIT License](LICENSE)。分发包包含的第三方组件适用各自的许可证；公开发布材料仍在整理中。
