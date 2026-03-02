---
description: 单片机工程中的自动编译烧录规则
triggers:
  - file_pattern: "**/User/main.c"
    event: [modified, saved]
  - file_pattern: "**/Hardware/*.c"
    event: [modified, saved]
  - file_pattern: "**/System/*.c"
    event: [modified, saved]
  - file_pattern: "**/User/*.c"
    event: [modified, saved]
---

# 单片机工程中的情形

## MCU 代码修改后自动编译烧录规则

当用户修改并保存单片机 C 代码文件时：

1. **确认编译烧录**：询问用户是否立即编译并烧录到开发板
   - 如果用户确认：执行 `/mcu-build-flash` workflow
   - 如果用户拒绝：记录待编译任务，稍后提醒

2. **检查项目完整性**：
   - 确认 `project.uvproj` 存在
   - 确认文件引用正确（无缺失的 .c/.h 引用）
   - 如有引用错误，提示用户修复

3. **编译前检查**：
   - 检查上次编译是否成功
   - 如上次失败，提示用户检查代码错误

4. **自动执行流程**：
   ```
   [1/4] 编译项目
   [2/4] 检查编译结果（必须读取 project.build_log.htm）
         - 检查文件: 02-01-Clock_Timer/Objects/project.build_log.htm
         - 确认: 0 Error(s)
         - 确认: creating hex file
         - 如检测到错误，显示完整错误信息并停止
   [3/4] 验证HEX文件已更新
         - 检查: {项目根目录}\Objects\project.hex 修改时间
         - 确认: HEX时间 > 所有源代码修改时间
         - 如HEX未更新，报错停止或重新编译
   [4/4] 烧录到开发板（提示冷启动）
   ```

5. **失败处理**：
   - 编译失败：显示错误日志，提示修复代码
   - 烧录失败：提示重试机制（最多5次，每次需断电上电）

## 触发条件

此规则在以下文件保存时触发：
- `User/main.c`
- `User/*.c`
- `Hardware/*.c`
- `System/*.c`
