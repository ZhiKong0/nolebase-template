
# openclaw cmd
openclaw config set tools.profile full---读写文件权限
openclaw gateway start---启动网关
openclaw gateway stop|停止网关
openclaw gateway status|查看状态
openclaw gateway restart|重启网关
openclaw doctor|诊断问题
openclaw doctor --fix|自动修复问题
openclaw configure|配置向导


# codex-AGENT.MD-setting
```

每次完成任何一个任务后，必须先把结果发到飞书端的 OpenClaw，然后再把“下一步要做什么”作为一条新的消息发进当前飞书 OpenClaw 会话，不允许把这两件事混成同一条消息。

执行规则如下：

1. 先整理一段简洁中文结果摘要，只包含：
- 做了什么
- 改了哪些文件或配置
- 当前结果

2. 不要把下面这些内容混进这条可见结果消息：
- 下一步建议
- 请基于以上结果继续给我下一步任务
- 请继续执行
- 本轮完成后请继续

3. 把可见结果写入：
E:\OpenClaw\.openclaw\workspace\openclaw_ops\auto_mode_summary.txt

4. 把真正要发给 OpenClaw 的下一步指令，单独写入：
E:\OpenClaw\.openclaw\workspace\openclaw_ops\auto_mode_next_step.txt

5. 然后统一运行：
powershell -NoProfile -ExecutionPolicy Bypass -File E:\OpenClaw\.openclaw\workspace\openclaw_ops\codex_finish_and_continue.ps1

6. 这条脚本会自动完成两件事：
- 先把 auto_mode_summary.txt 作为一条可见进度消息发送到飞书
- 再把 auto_mode_next_step.txt 作为一条新的桌面飞书消息发进 OpenClaw[WIN] 会话，让 OpenClaw 把它当成新的用户输入

7. 不要再额外调用：
- send_openclaw_continue_prompt.ps1
- 旧的单条 openclaw message send 末尾拼接“请继续给我下一步任务”的做法

8. 只有在飞书发送成功之后，才能在当前对话里回复我。
回复时要带上：
- 已发送到飞书
- 本次发送返回的 messageId

9. 自动模式下，默认职责分离如下：
- 可见进度回报：message send
- 下一步任务指令：桌面飞书脚本发进 OpenClaw[WIN]
- OpenClaw 收到后，再由它自己调用脚本把下一条任务发到 VS Code Codex

10. 不要把 OpenClaw 视角的话术混进可见进度消息。
可见进度消息只做汇报，不负责驱动下一轮。


```