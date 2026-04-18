# MCU 编译烧录工具

## 位置

- 主程序: `000Project_PC_Control\mcu_build_flash_gui.py`
- 启动脚本: `000Project_PC_Control\run_mcu_build_flash_gui.bat`
- 运行目录: `%LOCALAPPDATA%\MCUBuildFlashGUI`

## 功能

- 选择 `COM` 口
- 选择烧录方式:
  - `STM32CubeProgrammer / ST-LINK`
  - `pyOCD / DAPLink`
  - `stcgal / 串口`
- 主窗口为迷你布局，只保留高频操作
- `高级设置` 使用独立弹窗
- `完整日志` 使用独立弹窗
- 调用 `Keil UV4` 执行编译
- 检查 `project.build_log.htm` 是否包含 `0 Error(s)`
- 检查 `HEX/AXF` 是否为最新产物
- 日志预览保留在主窗口底部

## 使用

1. 双击运行 `run_mcu_build_flash_gui.bat`
2. 在主窗口选择工程目录、烧录方式和 `COM`
3. 如需改 `UV4.exe`、`pyocd` 等路径，点击 `高级设置`
4. 点击 `编译并烧录`
5. 如需看完整输出，点击 `完整日志`

## 说明

- 启动脚本会优先使用 `pythonw.exe`，并把工作目录、状态文件、`pycache` 都切到 `%LOCALAPPDATA%\MCUBuildFlashGUI`
- 这样可以尽量避免和项目目录下其他正在运行的 Python 程序互相影响
- `ST-LINK` 与 `pyOCD` 不依赖 `COM` 口，界面仍保留 `COM` 选择，便于统一记录
- `pyOCD` 会先执行 `erase --chip` 再 `load`
- `stcgal` 会按配置次数重试，执行时需要按日志提示给目标板断电上电
