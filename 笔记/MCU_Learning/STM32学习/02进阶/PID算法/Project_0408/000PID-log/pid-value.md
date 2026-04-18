python .\exp_runner.py --port COM18 --baud 115200 --id 6033 --ms 35000 --out .\000Data --observe-safe --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.10 --at 0 --hp 12400 --hd 0.005 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 250 --ramp 2 --realtime

- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.015 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2
- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0157 --at 0 --hp 3.00 --hd 0.015 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2
- **python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.050 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2**
- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.080 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2
- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.060 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2
- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.050 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2

- python .\exp_runner.py --observe-safe --port COM18 --baud 115200 --id 6082 --ms 30000 --out .\000Data --pwm-max 60 --diff-max 20 --spd 2 --so 90 --trim 0.0457 --at 0 --hp 3.00 --hd 0.150 --hs 0 --db 1.0 --hi 0.005 --hil 0.5 --min 12 --kp 21 --km 160 --ramp 2





- python exp_runner.py --port COM18 --ms 60000 --spd 1 --skp 0.1 --ski 0.012 --skd 0 --akp 0.50 --aki 0.0024 --akd 0.32 --realtime
- python exp_runner.py --port COM18 --ms 60000 --spd 1 --skp 0.1 --ski 0.005 --skd 0 --akp 0.75 --aki 0 --akd 0.45 --realtime
- python exp_runner.py --port COM18 --ms 60000 --spd 1 --skp 0.1 --ski 0.005 --skd 0 --akp 3.00 --aki 0 --akd 0.45 --realtime
- python exp_runner.py --port COM18 --ms 30000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.3 --aki 0 --akd 0.08 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 30000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.3 --aki 0 --akd 0.50 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 10000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.3 --aki 0 --akd 10.00 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 10000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.3 --aki 0 --akd 0.12 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 10000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.3 --aki 0 --akd 10.00 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 30000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.35 --aki 0 --akd 0.12 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 30000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 0.36 --aki 0 --akd 0.12 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
- python exp_runner.py --port COM18 --ms 40000 --out 000Data --ts 35 --skp 0.6 --ski 0.012 --skd 0 --akp 1.0 --aki 0 --akd 0.12 | Select-String -Pattern 'EXPERIMENT=','MCU_EXP_ID=','TRAJ ','ONSET ','MID_PAUSE ','BIAS ','PATTERN ','PHASE ','RAW=','ANALYSIS=','WINDOWS=','TRAJECTORY='
