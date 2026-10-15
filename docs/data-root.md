# 数据目录

索引、内容搜索数据库、预览增强包、程序配置、会话与日志都放在同一个数据目录下。默认位置是 `%LOCALAPPDATA%\Pulse`；把数据放到别的盘（例如程序所在目录）有两种方式，按优先级从高到低：

1. **环境变量 `PULSE_DATA_DIR`**：设为绝对路径，例如 `PULSE_DATA_DIR=D:\PulseData`。对当前用户的所有 Pulse 进程生效，包括从命令行启动的辅助进程。
2. **程序同目录的 `pulse.ini`**：写入一行 `data_dir=<路径>`，例如
   - `data_dir=.` —— 数据放在程序所在目录（免安装版默认）；
   - `data_dir=D:\PulseData` —— 绝对路径；
   - `data_dir=..\PulseData` —— 相对于程序目录。
   以 `#` 或 `;` 开头的行是注释。删除该文件即恢复默认位置。

两种方式都要求目标可写；不可写时自动回退到 `%LOCALAPPDATA%\Pulse`，不会导致程序无法启动。

免安装版的 `pulse.ini` 默认内容是 `data_dir=.`，因此索引、配置和日志都随程序目录携带，整个文件夹拷走即可整体迁移。安装版不附带该文件，行为与之前一致。

**PulseIndex 服务（仅安装版）**的机器级索引配置仍在 `%PROGRAMDATA%\Pulse`，不随以上方式迁移：服务以系统账户运行，不继承用户环境变量。服务版迁移索引位置请用设置内的「索引位置」选项，见[索引存储与迁移](index-migration.md)。

## 迁移已有数据

换数据目录不会自动搬运旧数据。关闭 Pulse 后运行：

```powershell
pwsh ./scripts/migrate_data_root.ps1 -Target D:\PulseData
```

脚本把 `%LOCALAPPDATA%\Pulse` 复制到目标目录并核对文件数，成功后把旧目录改名为 `Pulse.migrated-<时间戳>`（可随时删回）。确认新位置工作正常后再删除该备份目录。
