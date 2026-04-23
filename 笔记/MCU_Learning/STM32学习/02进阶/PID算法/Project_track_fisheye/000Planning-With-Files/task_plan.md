# Task Plan: 实验记录器接管

## Goal
停掉当前 `TRACK` 自适应调参，占用 `COM18` 的只保留 `experiment_logger`，让用户后续每次按下启停都能自动落一份实验串口日志。

## Current Phase
Phase 4

## Phases

### Phase 1: 清理后台调参与串口占用
- [x] 检查是否仍有 `track_adaptive_tuner` 后台进程
- [x] 确认 `COM18` 不再被自动调参占用
- **Status:** complete

### Phase 2: 核对记录器逻辑
- [x] 读取 `experiment_logger.ps1`
- [x] 读取 `experiment_logger.py`
- [x] 确认记录触发条件是 `EVT:EXP_START / EVT:EXP_STOP`，并支持 `HB` 补开文件
- **Status:** complete

### Phase 3: 修正记录器运行体验
- [x] 将 `experiment_logger.ps1` 改为 `python -u`
- [x] 将 `experiment_logger.py` 改为行缓冲输出
- [x] 为 `#EXP?!` 初始同步加入重试，减少假警告
- **Status:** complete

### Phase 4: 常驻启动与验证
- [x] 启动 `experiment_logger` 常驻进程
- [x] 确认 `powershell.exe + python.exe` 两层进程已存在
- [x] 确认启动日志已经实时写入 `logger_session_*.out.txt`
- [x] 已捕获真实启停并生成新的 `exp_0254 ~ exp_0257`
- **Status:** complete

## Decisions Made
- 当前先暂停自动调参，不再占用串口。
- 记录器继续使用 `COM18`。
- 记录目录保持为 `Project_track_fisheye\\000Data\\serial_runs\\experiments`。
- 启动日志走 `logger_session_*.out.txt / err.txt`，实验正文走 `exp_*.txt`。

## Hot Files
- `000Project_PC_Control/experiment_logger.ps1`
- `000Project_PC_Control/experiment_logger.py`
