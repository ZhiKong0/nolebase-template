---
description: MCU项目编译与烧录操作指南
---

# 蓝桥杯MCU项目编译与烧录操作指南

---

## 一、路径变量定义（需根据实际项目修改）

**必需变量:**

- `{项目根目录}`: Keil项目文件(.uvproj)所在目录
- `{UV4路径}`: Keil UV4.exe 安装路径（通常是 `D:\keil\Keil-v5\C51\UV4\UV4.exe`）
- `{串口号}`: CH340对应的COM口（如 COM3/COM6）

**示例:**

```
项目根目录 = f:\Project\01-倒计时
UV4路径 = D:\keil\Keil-v5\C51\UV4\UV4.exe
串口号 = COM6
```

---

## 二、编译方法

### 2.1 使用Keil命令行编译

**模板命令:**

```bash
{UV4路径} -b "{项目根目录}\project.uvproj" -j0 -t "Target 1" -o "{项目根目录}\Objects\project.build_log.htm"
```

**实际示例:**

```bash
D:\keil\Keil-v5\C51\UV4\UV4.exe -b "f:\Documents\0827\Learning Something\MCU_Learning\蓝桥杯C51学习\Project\001Try\test\02-倒计时程序_使用库函数版\project.uvproj" -j0 -t "Target 1" -o "f:\Documents\0827\Learning Something\MCU_Learning\蓝桥杯C51学习\Project\001Try\test\02-倒计时程序_使用库函数版\Objects\project.build_log.htm"
```

**参数说明:**

- `-b`: 编译项目
- `-j0`: 输出详细信息
- `-t "Target 1"`: 指定目标
- `-o`: 输出编译日志到指定文件

---

### 2.2 编译结果查看

**编译日志文件:** `{项目根目录}\Objects\project.build_log.htm`

**关键信息:**

- `0 Error(s)`: 编译成功
- `Program Size: data=xx.x xdata=0 code=xxxx`: 程序大小
- `creating hex file`: 成功生成HEX文件

---

### 2.3 验证HEX文件已更新（重要）

**目的:** 确保烧录的是最新编译的代码，避免烧录旧版本

**验证步骤:**

1. **检查HEX文件修改时间**
   ```bash
   # Windows PowerShell
   Get-Item "{项目根目录}\Objects\project.hex" | Select-Object FullName, LastWriteTime
   ```

2. **检查源代码最新修改时间**
   ```bash
   # Windows PowerShell
   Get-ChildItem "{项目根目录}\User", "{项目根目录}\Hardware", "{项目根目录}\System" -Recurse -Include *.c,*.h | Sort-Object LastWriteTime -Descending | Select-Object -First 1 FullName, LastWriteTime
   ```

3. **确认时间关系**
   - **HEX时间 > 所有源代码修改时间** → 验证通过
   - **HEX时间 < 某个源代码修改时间** → 需要重新编译

**验证失败处理:**
- 如果HEX未更新或时间不对，**重新执行编译命令**
- 检查编译日志是否显示 `creating hex file`

---

### 2.4 编译失败排查

#### 2.4.1 常见编译错误

| 错误信息                                            | 原因                       | 解决方法                             |
| --------------------------------------------------- | -------------------------- | ------------------------------------ |
| `error C318: can't open file 'xxx.c'`             | 项目引用了不存在的文件     | 检查project.uvproj中的文件引用       |
| `error C141: syntax error near ')'`               | 函数声明与定义参数不匹配   | 统一参数名（如 `data` vs `dat`） |
| `error C193: '<<': bad operand type`              | C51位变量不能直接移位      | 先强制转换为 `u8`再移位            |
| `error C236: different length of parameter lists` | 函数声明与定义参数数量不同 | 检查声明和定义是否一致               |

#### 2.4.2 路径引用问题排查（重要）

**现象:** 编译报错 `error C318: can't open file 'Hardware\Key.c'` 等

**根本原因:** Keil项目文件(project.uvproj)引用了不存在的库文件

**排查步骤:**

1. **打开项目配置文件**

   - 文件路径: `{项目根目录}\project.uvproj`
   - 用文本编辑器打开（记事本、VS Code等）
2. **查找文件引用节点**

   - 搜索 `<FileName>` 标签
   - 检查每个引用的 `.c` 和 `.h` 文件是否实际存在
3. **检查Groups结构**

   ```xml
   <Groups>
     <Group>
       <GroupName>Hardware</GroupName>
       <Files>
         <File>
           <FileName>Key.c</FileName>  ← 检查此文件是否存在
           <FilePath>.\Hardware\Key.c</FilePath>
         </File>
       </Files>
     </Group>
   </Groups>
   ```
4. **修复方法（二选一）**

   **方法A - 删除不存在的引用（推荐）:**

   ```xml
   <!-- 修改前 -->
   <Group>
     <GroupName>Hardware</GroupName>
     <Files>
       <File>
         <FileName>Key.c</FileName>
         <FilePath>.\Hardware\Key.c</FilePath>
       </File>
     </Files>
   </Group>

   <!-- 修改后 -->
   <Group>
     <GroupName>Hardware</GroupName>
   </Group>
   ```

   **方法B - 创建缺失的文件:**

   - 在对应目录创建空的 `.c` 和 `.h` 文件
5. **验证修复**

   - 保存 `project.uvproj`
   - 重新编译

---

## 三、查找CH340串口

### 3.1 使用Python查找串口

**命令:**

```bash
python -c "import serial.tools.list_ports as s; ports=s.comports(); [print(f'{p.device}: {p.description}') for p in ports]"
```

**查找CH340端口:**

```bash
python -c "import serial.tools.list_ports as s; ports=s.comports(); ch340=[p for p in ports if 'CH340' in p.description]; print(ch340[0].device if ch340 else 'CH340 not found')"
```

### 3.2 常见串口设备

- `COM6: USB-SERIAL CH340 (COM6)` - 蓝桥杯开发板常用

---

## 四、烧录方法

### 4.1 使用stcgal命令行烧录

**前提条件:**

- 已安装stcgal: `pip install stcgal`

**烧录模板命令:**

```bash
stcgal -P stc15 -p {串口号} "{项目根目录}\Objects\project.hex"
```

**实际示例:**

```bash
stcgal -P stc15 -p COM6 "f:\Documents\0827\Learning Something\MCU_Learning\蓝桥杯C51学习\Project\001Try\test\02-倒计时程序_使用库函数版\Objects\project.hex"
```

**参数说明:**

- `-P stc15`: 指定STC15系列单片机
- `-p COM6`: 指定串口号（根据实际修改）
- 最后一个参数: HEX文件完整路径

---

### 4.2 烧录流程

1. **断开开发板电源**（拔USB线）
2. **等待2秒**
3. **重新上电**（插回USB线）
4. **等待stcgal自动识别并烧录**
5. **烧录成功提示:**
   ```
   Setting options: done
   Target UID: XXXXXXXXXXXX
   Disconnected!
   ```

---

### 4.3 烧录失败排查

- 检查串口号是否正确
- 确认开发板已断电再上电（冷启动）
- 检查USB线连接
- 尝试其他COM口
- **尝试多次烧录**（见4.4节）

---

### 4.4 烧录失败重试机制（重要）

**问题现象:**

- 烧录时显示 `Serial port error` 或 `Waiting for MCU` 超时
- 有时单次烧录会失败，需要多次尝试

**解决方法 - 多次重试:**

如果烧录失败，**请连续尝试烧录最多5次**，步骤如下：

| 次数          | 操作                                   |
| ------------- | -------------------------------------- |
| 第1次         | 执行烧录命令 → 断电上电 → 观察结果   |
| 第2次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第3次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第4次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第5次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第5次后仍失败 | 停止烧录，检查硬件连接或更换USB线      |

**注意事项:**

- 每次烧录前必须**断电再上电**（冷启动）
- 断电后等待2秒再上电
- 确保USB线连接牢固
- 如果5次都失败，可能是硬件问题

---

## 五、完整自动化流程

### 5.1 编译+烧录一键脚本

**基础版本:**

```batch
@echo off
chcp 65001 >nul
set 项目根目录=f:\Project\01-倒计时
set UV4路径=D:\keil\Keil-v5\C51\UV4\UV4.exe
set 串口号=COM6

echo [1/3] 正在编译...
%UV4路径% -b "%项目根目录%\project.uvproj" -j0 -t "Target 1"
if errorlevel 1 (
    echo 编译失败!
    pause
    exit /b 1
)

echo [2/3] 编译成功，准备烧录...
echo [3/3] 请给开发板断电再上电，然后按任意键继续...
pause >nul

stcgal -P stc15 -p %串口号% "%项目根目录%\Objects\project.hex"

echo.
if %errorlevel% == 0 (
    echo 烧录成功！
) else (
    echo 烧录失败，请重试（最多5次）
)
pause
```

**带自动重试的版本:**

```batch
@echo off
chcp 65001 >nul
echo ========================================
echo 蓝桥杯MCU倒计时程序 - 一键编译烧录工具
echo ========================================

set 项目根目录=f:\Project\01-倒计时
set UV4路径=D:\keil\Keil-v5\C51\UV4\UV4.exe
set 串口号=COM6
set 烧录次数=0
set 最大次数=5

echo.
echo [1/3] 正在编译...
%UV4路径% -b "%项目根目录%\project.uvproj" -j0 -t "Target 1"
if errorlevel 1 (
    echo [错误] 编译失败! 请检查代码错误。
    pause
    exit /b 1
)
echo [成功] 编译完成！

echo.
echo [2/3] 准备烧录...
echo HEX文件: %项目根目录%\Objects\project.hex

echo.
echo ========================================
echo [3/3] 开始烧录（最多%最大次数%次尝试）
echo ========================================
echo 【重要】当显示 "Waiting for MCU" 时:
echo   1. 拔掉开发板USB线（断电）
echo   2. 等待2秒
echo   3. 插回USB线（上电）
echo.

:retry_loop
set /a 烧录次数+=1
echo.
echo ---------- 第 %烧录次数% / %最大次数% 次尝试 ----------

stcgal -P stc15 -p %串口号% "%项目根目录%\Objects\project.hex"
if %errorlevel% == 0 (
    echo.
    echo ========================================
    echo 烧录成功！
    echo ========================================
    pause
    exit /b 0
)

echo.
echo [失败] 第 %烧录次数% 次烧录失败。
if %烧录次数% lss %最大次数% (
    echo 请按任意键进行下一次尝试...
    pause >nul
    goto retry_loop
) else (
    echo ========================================
    echo 已达到最大重试次数(%最大次数%)，烧录失败。
    echo 请检查:
    echo  - 开发板是否连接
    echo  - 串口号是否正确
    echo  - USB线是否完好
    echo  - 是否进行了断电再上电操作
    echo ========================================
    pause
    exit /b 1
)
```

---

## 六、项目结构说明

```
02-倒计时程序_使用库函数版/
├── User/
│   └── main.c              # 主程序文件（包含所有驱动代码）
├── Objects/
│   ├── project.hex         # 编译生成的烧录文件
│   └── project.build_log.htm  # 编译日志
├── System/                 # 系统文件夹（可选，用于存放库文件）
├── Hardware/               # 硬件文件夹（可选，用于存放库文件）
├── StartUp/
│   └── STARTUP.A51         # 启动文件
├── project.uvproj          # Keil项目文件
└── 烧录.bat                # 一键烧录脚本
```

**从零开始的项目特点:**

- System/和Hardware/文件夹为空
- 所有代码集中在User/main.c中
- 项目配置只引用main.c和STARTUP.A51

---

## 七、从零开始的开发流程

### 7.1 初始化项目

1. 清空System/文件夹
2. 清空Hardware/文件夹
3. 保留User/main.c
4. 修改project.uvproj，删除所有不存在的文件引用

### 7.2 编写代码

1. 在main.c中按步骤编写代码
2. 每完成一个功能就编译测试
3. 遇到错误时参考"2.3 编译失败排查"

### 7.3 编译烧录

1. 编译生成HEX文件
2. 使用烧录脚本或命令行烧录
3. 如失败，按"4.4 重试机制"最多尝试5次

---

## 八、常见问题

### Q: 编译报错 "syntax error near 'u8'"

A: C51编译器要求变量声明必须在函数开头，将变量声明移到函数开始处。

### Q: 数码管不显示

A: 检查:

1. 跳线帽是否插好
2. 138译码器地址是否正确(P2^7=0使能)
3. 扫描频率是否足够(>100Hz)

### Q: 按键抖动

A: 添加软件消抖:

- 连续3次(30ms)扫描相同才确认按键
- 使用边沿检测，只触发一次

### Q: 烧录失败"Waiting for MCU"

A: 需要给开发板断电再上电(冷启动)才能进入下载模式。如果一次失败，请重试最多5次。

### Q: 编译报错 "can't open file 'Hardware\Key.c'"

A: 项目引用了不存在的库文件。请打开project.uvproj，删除Hardware/和System/组中不存在的文件引用。

---

## 九、相关工具路径

- **Keil UV4:** `D:\keil\Keil-v5\C51\UV4\UV4.exe`
- **stcgal:** 通过 `pip install stcgal` 安装

---

**文档生成时间:** 2025年-2026年
**适用于:** 蓝桥杯CT107D开发板 + STC15F2K60S2单片机
**版本:** v3.0（增加HEX文件验证步骤）
