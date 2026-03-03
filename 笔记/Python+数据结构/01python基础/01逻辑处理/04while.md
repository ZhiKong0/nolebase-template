# Python while 循环详解

> while 循环用于在条件为真时重复执行代码块，适合**次数不确定**的循环场景。

---

## 目录

1. [基础语法](#一基础语法)
2. [计数器循环](#二计数器循环)
3. [无限循环与 break](#三无限循环与-break)
4. [循环控制：continue 与 else](#四循环控制continue-与-else)
5. [while vs for 对比](#五while-vs-for-对比)
6. [嵌套 while 循环](#六嵌套-while-循环)
7. [常见错误与调试](#七常见错误与调试)
8. [实战练习](#八实战练习)

---

## 一、基础语法

### 1.1 基本结构

```python
while 条件表达式:
    # 循环体（缩进4个空格）
    执行语句
# 条件为False时继续执行
```

### 1.2 最简单的循环

```python
# 打印 1 到 5
count = 1
while count <= 5:
    print(count)
    count += 1  # 必须手动更新条件变量！

# 输出:
# 1
# 2
# 3
# 4
# 5
```

**⚠️ 注意**：忘记 `count += 1` 会导致**无限循环**！

---

## 二、计数器循环

### 2.1 基础计数器

```python
# 倒计时
countdown = 5
while countdown > 0:
    print(f"倒计时: {countdown}")
    countdown -= 1
print("发射！")

# 输出:
# 倒计时: 5
# 倒计时: 4
# 倒计时: 3
# 倒计时: 2
# 倒计时: 1
# 发射！
```

### 2.2 累加求和

```python
# 计算 1+2+3+...+100
total = 0
n = 1
while n <= 100:
    total += n
    n += 1
print(f"1到100的和: {total}")  # 5050
```

### 2.3 累乘（阶乘）

```python
def factorial(n):
    """计算n的阶乘"""
    if n < 0:
        return None
    result = 1
    i = 1
    while i <= n:
        result *= i
        i += 1
    return result

print(factorial(5))  # 120 (5*4*3*2*1)
```

---

## 三、无限循环与 break

### 3.1 无限循环基础

```python
# 无限循环（需要手动break退出）
while True:
    user_input = input("请输入'quit'退出: ")
    if user_input == "quit":
        break  # 跳出循环
    print(f"你输入了: {user_input}")
```

### 3.2 游戏循环模式

```python
import random

# 猜数字游戏
target = random.randint(1, 100)
attempts = 0

while True:
    guess = int(input("猜一个1-100的数字: "))
    attempts += 1
    
    if guess < target:
        print("太小了！")
    elif guess > target:
        print("太大了！")
    else:
        print(f"恭喜！你用了{attempts}次猜中了！")
        break  # 猜中后退出
```

### 3.3 读取数据直到结束

```python
# 模拟读取文件直到EOF
def read_lines():
    """模拟逐行读取数据"""
    lines = []
    while True:
        line = input("输入一行（空行结束）: ")
        if line == "":
            break
        lines.append(line)
    return lines
```

---

## 四、循环控制：continue 与 else

### 4.1 continue - 跳过当前迭代

```python
# 打印1-10中的奇数
n = 0
while n < 10:
    n += 1
    if n % 2 == 0:
        continue  # 跳过偶数，直接进行下一次循环
    print(n)

# 输出: 1 3 5 7 9
```

### 4.2 while-else 结构

```python
# while-else: 循环正常结束（没有break）时执行else

def find_prime(start, end):
    """查找范围内的质数"""
    n = start
    while n <= end:
        i = 2
        while i * i <= n:
            if n % i == 0:
                break  # 不是质数
            i += 1
        else:  # 内层while正常结束，说明是质数
            print(f"{n} 是质数")
        n += 1

find_prime(10, 30)
# 11 是质数
# 13 是质数
# 17 是质数
# 19 是质数
# 23 是质数
# 29 是质数
```

**关键点**：
- `break` 执行了 → `else` **不执行**
- `break` 没执行（正常结束）→ `else` **执行**

### 4.3 对比表

| 语句 | 作用 | 使用场景 |
|:-----|:-----|:---------|
| `break` | 立即终止整个循环 | 找到目标后提前退出 |
| `continue` | 跳过当前迭代 | 过滤某些条件 |
| `else` | 循环正常结束时执行 | 处理"未找到"等情况 |

---

## 五、while vs for 对比

### 5.1 何时用 while？

| 场景 | 推荐 |
|:-----|:-----|
| 次数不确定 | ✅ while |
| 等待特定条件 | ✅ while |
| 用户输入验证 | ✅ while |
| 遍历已知序列 | ✅ for |
| 固定次数循环 | ✅ for |

### 5.2 代码对比

```python
# while 写法（适合：条件驱动）
# 场景：读取用户输入直到输入合法
while True:
    age = input("请输入年龄: ")
    if age.isdigit() and 0 < int(age) < 150:
        break
    print("输入不合法，请重试")

# for 写法（适合：遍历序列）
# 场景：处理列表中的每个元素
for item in items:
    process(item)
```

### 5.3 while 实现类似 range

```python
# for 写法
for i in range(5):
    print(i)

# while 等效写法
i = 0
while i < 5:
    print(i)
    i += 1
```

---

## 六、嵌套 while 循环

### 6.1 基础嵌套

```python
# 打印乘法表
i = 1
while i <= 9:
    j = 1
    while j <= i:
        print(f"{j}×{i}={i*j:2}", end="  ")
        j += 1
    print()  # 换行
    i += 1
```

### 6.2 控制嵌套循环

```python
# 找第一个满足条件的元素
matrix = [[1, 2], [3, 4], [5, 6]]
i = 0
found = False

while i < len(matrix) and not found:
    j = 0
    while j < len(matrix[i]):
        if matrix[i][j] > 3:
            print(f"找到: matrix[{i}][{j}] = {matrix[i][j]}")
            found = True
            break  # 跳出内层
        j += 1
    i += 1
```

### 6.3 打印图案

```python
# 打印金字塔
height = 5
row = 1
while row <= height:
    # 打印空格
    space = 1
    while space <= height - row:
        print(" ", end="")
        space += 1
    
    # 打印星号
    star = 1
    while star <= 2 * row - 1:
        print("*", end="")
        star += 1
    
    print()
    row += 1

# 输出:
#     *
#    ***
#   *****
#  *******
# *********
```

---

## 七、常见错误与调试

### 7.1 经典错误

```python
# ❌ 错误1：忘记更新条件变量（死循环）
count = 0
while count < 5:
    print(count)
    # 忘记 count += 1！

# ❌ 错误2：条件永远为真
count = 10
while count > 0:  # 如果 count 不会变，就是死循环
    print(count)

# ❌ 错误3：缩进错误
while x < 10:
print(x)  # IndentationError
```

### 7.2 调试技巧

```python
# 使用print调试
i = 0
while i < 5:
    print(f"DEBUG: i = {i}")
    i += 1

# 设置最大迭代次数（防止死循环）
max_iterations = 1000
count = 0
iteration = 0
while count < 10000:
    iteration += 1
    if iteration > max_iterations:
        print("警告：超过最大迭代次数，可能死循环")
        break
    # 正常逻辑
    count += 1
```

### 7.3 安全退出无限循环

```python
import signal
import sys

def signal_handler(sig, frame):
    print("\n收到退出信号，正在清理...")
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

while True:
    # 你的代码
    pass
```

---

## 八、实战练习

### 练习1：数字反转

```python
def reverse_number(n):
    """反转整数（如 123 → 321）"""
    reversed_num = 0
    is_negative = n < 0
    n = abs(n)
    
    while n > 0:
        digit = n % 10      # 取最后一位
        reversed_num = reversed_num * 10 + digit
        n //= 10            # 去掉最后一位
    
    return -reversed_num if is_negative else reversed_num

print(reverse_number(12345))   # 54321
print(reverse_number(-678))    # -876
```

### 练习2：猜数字游戏（带提示限制）

```python
import random

def guess_number_game():
    target = random.randint(1, 100)
    attempts = 0
    max_attempts = 7
    
    print("猜一个1-100的数字，你有7次机会")
    
    while attempts < max_attempts:
        guess = int(input(f"第{attempts + 1}次猜测: "))
        attempts += 1
        
        if guess < target:
            print("太小了！")
        elif guess > target:
            print("太大了！")
        else:
            print(f"恭喜！你用了{attempts}次猜中了！")
            return
    
    print(f"游戏结束！正确答案是 {target}")

guess_number_game()
```

### 练习3：验证密码强度

```python
def check_password_strength():
    """持续要求输入直到密码强度达标"""
    
    while True:
        password = input("设置密码（至少8位，包含大小写字母和数字）: ")
        
        has_upper = False
        has_lower = False
        has_digit = False
        
        i = 0
        while i < len(password):
            char = password[i]
            if char.isupper():
                has_upper = True
            elif char.islower():
                has_lower = True
            elif char.isdigit():
                has_digit = True
            i += 1
        
        if len(password) >= 8 and has_upper and has_lower and has_digit:
            print("密码设置成功！")
            return password
        else:
            print("密码强度不够，请重试")

check_password_strength()
```

### 练习4：模拟登录重试

```python
def login_system():
    """模拟登录，允许3次重试"""
    correct_username = "admin"
    correct_password = "123456"
    
    attempts = 3
    
    while attempts > 0:
        username = input("用户名: ")
        password = input("密码: ")
        
        if username == correct_username and password == correct_password:
            print("登录成功！")
            return True
        else:
            attempts -= 1
            if attempts > 0:
                print(f"登录失败，还剩{attempts}次机会")
            else:
                print("登录失败次数过多，账户已锁定")
                return False

login_system()
```

### 练习5：求最大公约数（欧几里得算法）

```python
def gcd(a, b):
    """使用辗转相除法求最大公约数"""
    while b != 0:
        a, b = b, a % b
    return a

print(gcd(48, 18))   # 6
print(gcd(100, 35))  # 5
```

---

## 速查表

| 用法 | 代码示例 |
|:-----|:---------|
| 基础循环 | `while i < 10:` |
| 无限循环 | `while True:` |
| 条件循环 | `while condition:` |
| 计数器 | `i = 0; while i < n: ... i += 1` |
| 提前退出 | `if x == target: break` |
| 跳过当前 | `if x < 0: continue` |
| while-else | `while x < 10: ... else: ...` |

---

## while vs for 选择指南

```
是否需要遍历一个已知的序列？
├── 是 → 用 for
└── 否 → 是否知道循环次数？
    ├── 是 → 用 for (range)
    └── 否 → 用 while（条件驱动）
```

---

> **总结**：while 循环是处理**条件驱动**和**次数不确定**场景的利器。使用时务必确保条件最终会变为False，避免死循环。配合 `break`、`continue`、`else` 可以实现复杂的循环控制逻辑。
