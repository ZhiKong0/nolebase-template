# Local Workflow Rules

完成任何任务后，必须执行以下收尾流程：

1. 在当前对话的最终回复中，明确给出 1 条“下一步优化 OpenClaw 的建议”。
2. 不允许只通过 `openclaw message send` 这类底层直发方式催办 OpenClaw。
3. 直接 IDE / manual-developer 任务优先使用统一收尾脚本：
   `E:\OpenClaw\.openclaw\workspace\openclaw_ops\codex_finish_manual.ps1`
4. 该脚本必须先发送可见结果摘要，再通过桌面自动化脚本向 OpenClaw 会话发送：
   `请你向codex发送继续下一步`
5. 如果任务收尾走自动模式脚本：
   `E:\OpenClaw\.openclaw\workspace\openclaw_ops\codex_finish_and_continue.ps1`
   则该脚本也必须完成上述桌面催办动作。

约束：

- 固定催办消息必须走飞书桌面窗口真实输入与发送链路。
- 不要把隐藏思维链发送到飞书。
- 如果桌面发送失败，必须报告具体失败脚本和错误。
- OpenClaw 自动化真正的权威规则文件是：
  `E:\OpenClaw\.openclaw\workspace\AGENTS.md`
