---
description: PID调试协作规则 — 每次执行前检查coordination共享工作区，读取其他AI的建议和监督意见
---

# PID 调试协作工作流

## 角色定义
- **Cascade (Windsurf)**: 主执行官，负责代码修改、编译烧录、参数调试
- **其他 AI**: 监督者，通过 coordination 文件夹提供建议、纠正错误

---

## A. 每次执行前必做 (PRE-CHECK)

### A1. 读取共享工作区
```
读取: coordination/PID_TUNING_CONTEXT.md
读取: coordination/CHANGELOG.md (最后20行，了解最近改动)
```
- 确认当前状态和待办事项

### A2. 检查监督意见
```
读取: coordination/SUPERVISOR_NOTES.md
```
- 如果有监督 AI 留下的纠正意见，**必须优先处理**
- 如果监督意见与当前计划冲突，**暂停执行并向用户确认**
- 处理完毕后在 SUPERVISOR_NOTES.md 对应条目标记 `[X] 已处理`

### A3. 检查新建议文件
```
检查: coordination/ 目录下是否有新的 REVIEW_*.md / SUGGESTION_*.md
```

---

## B. 每次代码改动后必做 (POST-CHANGE LOG)

**每次修改代码文件后，必须立即追加一条记录到 `coordination/CHANGELOG.md`**。
格式严格如下，每条记录包含时间、思考链、执行链、改动清单：

```markdown
---
## [YYYY-MM-DD HH:MM] — Cascade

### 思考链 (WHY)
- 问题现象: [简述观察到的问题]
- 根因分析: [分析为什么会出现这个问题]
- 决策理由: [为什么选择这个修复方案而非其他]

### 执行链 (WHAT)
- [x] 步骤1: [做了什么]
- [x] 步骤2: [做了什么]
- [ ] 步骤3: [待做]

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `path/to/file.c` | L123-130 | 描述改了什么 |

### 编译/测试结果
- 编译: [0 Error / N Error]
- 测试: [结果摘要，或"待测"]

### 待监督审查
> ⚠️ [需要监督AI审查的关键点]
```

**时间必须使用 UTC+8，格式 `YYYY-MM-DD HH:MM`。**

---

## C. 上下文同步

每完成一轮完整修改（改代码→编译→测试→得到结果），更新 `coordination/PID_TUNING_CONTEXT.md`:
- "变更时间线" 表格追加新行
- "当前已知问题" 更新状态
- "给监督AI的建议" 如有新发现则更新

---

## D. 监督 AI 工作协议

监督 AI 在 `coordination/SUPERVISOR_NOTES.md` 追加意见，格式:

```markdown
## [YYYY-MM-DD HH:MM] — [AI名称]
### 问题
[描述发现的问题]
### 建议
[具体修改建议，最好包含代码片段]
### 优先级
- [ ] 必须立即处理
- [ ] 下次执行时处理
- [ ] 仅供参考
### 状态
- [ ] 待处理
- [ ] 已处理
```

---

## E. 关键约束 (所有 AI 必须遵守)

1. **不要重新引入 yaw 预测** — P/D 必须用同一时间戳数据
2. **AKD 不超过 5.0** (新刻度) — IMU 20ms 延迟限制
3. **diff_max 必须防枢转** — 低侧轮 PWM 必须 >= MOTOR_DEADZONE
4. **速度环 core 不能被 deadzone 跳变破坏** — 前馈需保底 DEADZONE
5. **先稳定 PID，再调 HEADING_TRIM** — 不要同时调两个东西
6. **每次固件修改必须编译+验证 build_log** — 0 Error(s)
7. **IMU 更新必须在控制循环之前** — 保证数据新鲜度
8. **gyro LPF 只在新样本到达时执行** — 不要每 tick 重复滤波旧数据
