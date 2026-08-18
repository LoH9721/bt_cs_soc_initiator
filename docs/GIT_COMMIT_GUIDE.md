# Git 提交与上传操作手册

本手册用于本仓库（`bt_cs_soc_initiator_slc_failed_backup`）日常提交和上传。每次完成一个 CR、编译和实机验证通过后，按下面的步骤操作。

## 1. 一次性配置（只需做一次）

### 1.1 配置提交用户名和邮箱

打开 PowerShell，进入仓库目录：

```powershell
cd D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup
```

设置提交身份（改成你自己的名字和邮箱）：

```powershell
git config user.name "你的名字"
git config user.email "你的邮箱"
```

> 如果之前没配置过，`git commit` 会报错要求身份，所以先做这一步。

### 1.2 配置远程仓库

当前仓库还没有配置远程（`git remote -v` 无输出）。要让别人能拉取，需要添加远程地址：

```powershell
git remote add origin <远程仓库地址>
```

例如：

```powershell
git remote add origin http://svn-server/xxx/bt_cs_soc_initiator_slc_failed_backup.git
```

> 远程地址以你们实际使用的为准；如果没有远程仓库，也可以只做本地提交，跳过本节。

## 2. 每次提交的标准流程

### 2.1 先看状态

```powershell
git status --short
```

确认哪些文件改了、哪些是新文件。`M` 表示已修改，`??` 表示未跟踪的新文件。

### 2.2 查看改动内容

```powershell
git diff --stat        # 概览：哪些文件改了多少行
git diff               # 详细差异
```

建议提交前扫一眼 diff，确认没有把无关改动或临时文件带进来。

### 2.3 添加要提交的文件

只添加本 CR 涉及的文件（推荐，避免混入无关改动）：

```powershell
git add app.c user_phone/data/phone_sm.c docs/CR008_PASSIVE_HID_HARDENING_PLAN.md
```

如果确认全部都要提交：

```powershell
git add -A
```

> 添加后可用 `git status` 确认暂存区（第一列变 `A`/`M`）。

### 2.4 提交

```powershell
git commit -m "feat(cr008): 提交信息"
```

提交信息建议沿用本仓库风格，例如：

```text
feat(cr008): implement authorized Bond identity and passive link gating

- CR008-006 授权手机 Bond 身份关联
- CR008-008 启动恢复校验
- CR008-009 PEPS 授权链路与新鲜测距
- CR008-010 自动落锁只监听授权链路断开
```

如果一次只提交一个 CR：

```text
feat(cr008): CR008-010 自动落锁只监听授权 Passive 链路断开
```

### 2.5 推送到远程

首次推送需要带上 `-u` 建立跟踪关系：

```powershell
git push -u origin feature/cr008-passive-hid-hardening
```

之后的推送直接：

```powershell
git push
```

## 3. 查看与核对

```powershell
git log --oneline -10          # 最近 10 条提交
git show <commit号> --stat     # 查看某次提交改了哪些文件
git status                     # 工作区是否干净
```

## 4. 常用回退/修正

### 4.1 提交后想改提交信息

```powershell
git commit --amend
```

> 只改最近一次提交；如果已经 push 过，修改后需要 `git push --force-with-lease`，请谨慎。

### 4.2 想撤销暂存（还没提交）

```powershell
git restore --staged <文件>
```

### 4.3 想丢弃某个文件的本地改动

```powershell
git restore <文件>
```

> 该操作会覆盖文件内容，且不可恢复；确认不需要该改动再用。

### 4.4 想撤销最近一次提交但保留改动

```powershell
git reset --soft HEAD~1
```

> 会把改动放回暂存区，不会丢代码。

## 5. 注意事项

1. 每个 CR 尽量单独提交，便于交接时按 commit 追溯。
2. 不要提交编译产物、临时文件、调试缓存；`git status` 里出现不认识的文件时先确认来源。
3. 未跟踪文件（`??`）不会自动加入提交，需要 `git add` 显式添加。
4. 本仓库存在 LF/CRLF 行尾转换警告（`LF will be replaced by CRLF`），是 Windows 下正常提示，不影响提交。
5. 如果 push 被拒（远程有新提交），先拉取再推送：

```powershell
git pull --rebase
git push
```

6. 提交前建议完成：代码改动点确认 → 编译通过 → 实机验证通过 → `git diff` 检查 → 提交。

## 6. 下次新 CR 的固定流程

```text
讨论方案 -> 确认 -> 改代码 -> 你编译/实机 -> 通过
-> git status 检查 -> git add <本次文件> -> git commit -> git push
```
