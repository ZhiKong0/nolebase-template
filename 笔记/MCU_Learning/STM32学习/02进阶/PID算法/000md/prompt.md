- serial_term.py他能调的各种参数比如pid,速度，小车偏置等在固件c代码中可以实现吗，如果没有对应的串口接口，请补充，如果serial_term.py不能调pid，小车速度，小车运行时间，小车启停，也请补充，
  如果最后的数据不能拿去像Project_Refactor\exp_runner.py和Project_Refactor\trajectory_analyzer.py一样去分析，那也请帮我再增加新的方案，我需要的两点，第一是发送命令行不管是发执行py命令还是如何，要响应快，这样可以快速的多轮测试，且能将运行那几秒的串口收到的数据保存在000Data中；第二是能够用py算法精准分析出小车在运行脚本之后跑的数据，能够精准的分析出小车细致的每个时刻的偏航角偏置情况左右轮转向情况



- 直接调参数

  spd=7

  trim=-0.0625

  so=180

  kpp=2.0

  kpi=0.2

  kpd=0.0

  hp=...

  hd=...

  hs=...

  db=...

  hi=...

  hil=...

  pwm=150

  diff=0

  min=...

  kp=...

  km=...

  ramp=...

  at=...

  at_kp=...

  at_ki=...

  at_lim=...

  raw=30

  bin=3

  enc_l_sign=1

  enc_r_sign=-1

  启停/状态

  run

  stop

  stat

  cal

  hir

  at_r这些分别代表什么意思帮我写成一个md放在@000PID-log 中

  并且我需要HB tick=1211460 exp_id=0 t_ms=0 run=0 spd=5 y=-9.748598 ty=0.000000 e=9.748598 c=0.000000 hi=0.000000 yr=-0.023193 ax=1 ay=-9 az=3 gx=-1 gy=0 gz=0 who=0x47 addr=0x68 icm_ok=242292 icm_fail=0 L=0 R=0 el=0 er=0 ed=0 trim=0.0000 at=0.0000 pmax=60 dmax=20 ok=60572 fail=0 rx=0

  HB tick=1211480 exp_id=0 t_ms=0 run=0 spd=5 y=-9.748600 ty=0.000000 e=9.748600 c=0.000000 hi=0.000000 yr=0.008545 ax=1 ay=-9 az=3 gx=-1 gy=1 gz=1 who=0x47 addr=0x68 icm_ok=242296 icm_fail=0 L=0 R=0 el=0 er=0 ed=0 trim=0.0000 at=0.0000 pmax=60 dmax=20 ok=60573 fail=0 rx=0

  HB tick=1211500 exp_id=0 t_ms=0 run=0 spd=5 y=-9.748016 ty=0.000000 e=9.748016 c=0.000000 hi=0.000000 yr=-0.051575 ax=2 ay=-5 az=8 gx=-1 gy=-1 gz=-1 who=0x47 addr=0x68 icm_ok=242300 icm_fail=0 L=0 R=0 el=0 er=0 ed=0 trim=0.0000 at=0.0000 pmax=60 dmax=20 ok=60574 fail=0 rx=0串口不需要发那么多数据，只要发有用于调小车
  比如刚开始命令行启动run并开始录制的时候，小车需要输出当前的trim和pid等调参的值还有exp_id，后面每10ms都是直接输出方便py脚本去计算轨迹和偏向的参数即可，其他可以不输出；而且在我自己在串口助手软件手动启动run测试的时候不希望输出exp_id，也不希望计入数据到000Data

  但是如果修改了串口的参数之后如果对py去分析的数据有影响请修改py数据分析的脚本，尽可能去做不影响py数据分析脚本的改动



- 我想用的是角度环，然后加上速度控制，而不是速度环，角度环直接控制控制
  或者直接用类似这样的算法 左轮右轮速度 = (偏航角相对差)+ 目标值，这样去算可以吗，你来帮我优化实现