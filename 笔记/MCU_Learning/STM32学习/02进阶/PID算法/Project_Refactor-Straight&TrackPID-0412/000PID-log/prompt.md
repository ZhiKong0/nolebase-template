- 现在好了可以恢复读到yaw值了，但是我在vofa没有看到I0对应的yaw值，一直还是保持0，没有在0值左右抖动

- 现在好像又看不到yaw值了， 请你再帮我进行检查一下
- 好的目前的问题是，目前只检测的到I0是yaw，但是检测不到左右轮编码器的值，我希望是I1和I2，而且小车按下key之后小车一直在左右蛇形走位并且整体还偏右边，无法去走直线，请你参照pid-params的最新参数，看看最新参数是不是还是这样，因为我换了电机编码器，从ICM换到了BNO，所以难免会有些问题，现在就是想吧参数调整调直，如果用py算法数据分析之后还是蛇形走位，请你帮我继续和以前一样复测给参数，复测，一直到py数据分析能够细致地分析出走的是直线为止

- 我看到的现象是小车刚开始开跑就，开始左右晃动，然后慢慢开始好一些，但是还是会抖动，你看你的py代码能不能分析出来我看到的现象，如果不能请修改并让他更加详细一点。而且第二我需要你每次运行收发串口的时候都加上试验编号比如expxx，并且将文件放在data文件夹下留给py数据分析，而且后面的编译烧录请你自行运行，如果只是需要改参数可以用exp_runner里面的接口，要求用到pid的算法，根据具体情况改pid的值python -c "import os; from exp_runner import ExpParams, run_one_exp; params=ExpParams(spd=2, so=90, min_pwm=12, kick_pwm=21, kick_ms=250, ramp=2, hp=10.75, hd=0.005, hs=0.0, db=1.0, hi=0.005, hil=0.5, kpp=2.0, kpi=0.2, kpd=0.0, trim=0.0956, at=0, at_kp=0.0015, at_ki=0.00015, at_lim=4.0, cal_wait_s=1.5); raw,csv=run_one_exp(port='COM18', baud=115200, exp_id=7004, exp_ms=15000, params=params, enc_l_sign=None, enc_r_sign=None, pwm_max=60, diff_max=20, bin_mode=None, out_dir=os.path.join(os.getcwd(),'000Data'), print_realtime=False, pause_after=False, quick=False, fast_start=False, no_verify=False, no_cal=False, no_dump=False, raw_pwm=None); print('RAW=' + str(raw)); print('CSV=' + str(csv))"这样的代码卡太久了，请你改成这样python .\exp_runner.py --port COM18 --baud 115200 --id 6033 --ms 35000 --out .\000Data --observe-safe --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.10 --at 0 --hp 10.00 --hd 0.005 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 250 --ramp 2 --realtime的去跑

- 第二轮是小车直行后一会就右转了然后纠正，后面就是左右抖动
- 