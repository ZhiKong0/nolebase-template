# Python for 循环详解

> 循环是编程中最核心的控制结构之一，Python 的 for 循环设计简洁优雅，功能强大。

---

## 目录

1. [基础语法](#一基础语法)
2. [遍历可迭代对象](#二遍历可迭代对象)
3. [range() 函数详解](#三range-函数详解)
4. [enumerate() 与 zip()](#四enumerate-与-zip)
5. [列表推导式](#五列表推导式)
6. [嵌套循环](#六嵌套循环)
7. [循环控制：break / continue / else](#七循环控制)
8. [迭代器原理](#八迭代器原理)
9. [常见错误与调试](#九常见错误与调试)
10. [实战练习](#十实战练习)

---

## 一、基础语法

### 1.1 基本结构

```python
for 变量 in 可迭代对象:
    # 循环体（缩进4个空格）
    执行语句
# 循环结束后继续执行
```

### 1.2 最简单的循环

```python
# 遍历列表
fruits = ["apple", "banana", "cherry"]
for fruit in fruits:
    print(fruit)

# 输出:
# apple
# banana
# cherry
```

### 1.3 遍历字符串

```python
# 字符串也是可迭代对象
for char in "Python":
    print(char, end="-")
# 输出: P-y-t-h-o-n-
```

---

## 二、遍历可迭代对象

### 2.1 遍历列表

```python
numbers = [1, 2, 3, 4, 5]

# 方式1：直接遍历元素
for num in numbers:
    print(num ** 2)  # 输出每个数的平方

# 方式2：通过索引遍历（不推荐，有更好的方法）
for i in range(len(numbers)):
    print(f"索引{i}: {numbers[i]}")
```

### 2.2 遍历字典

```python
student = {"name": "张三", "age": 20, "score": 85}

# 遍历键（默认）
for key in student:
    print(key)

# 遍历键值对
for key, value in student.items():
    print(f"{key}: {value}")

# 只遍历值
for value in student.values():
    print(value)
```

### 2.3 遍历元组和集合

```python
# 元组（不可变）
point = (3, 4)
for coord in point:
    print(coord)

# 集合（无序、不重复）
unique_nums = {1, 2, 3, 3, 3}  # 实际为 {1, 2, 3}
for num in unique_nums:
    print(num)  # 顺序不确定
```

---

## 三、range() 函数详解

### 3.1 range() 三种用法

```python
# range(stop) - 从0开始，到stop-1结束
for i in range(5):      # 0, 1, 2, 3, 4
    print(i)

# range(start, stop) - 从start开始，到stop-1结束
for i in range(2, 6):   # 2, 3, 4, 5
    print(i)

# range(start, stop, step) - 带步长
for i in range(0, 10, 2):   # 0, 2, 4, 6, 8
    print(i)

# 负数步长（倒序）
for i in range(5, 0, -1):   # 5, 4, 3, 2, 1
    print(i)
```

### 3.2 range() 特性

```python
# range() 返回的是 range 对象（惰性求值）
r = range(1000000)  # 不占大量内存

# 可以转换为列表
even_numbers = list(range(0, 10, 2))  # [0, 2, 4, 6, 8]

# 支持索引访问
print(range(10)[3])   # 3
print(range(10)[-1])  # 9
```

### 3.3 常见应用场景

```python
# 重复执行N次
for _ in range(3):
    print("重复执行")

# 生成索引遍历列表
names = ["Alice", "Bob", "Charlie"]
for i in range(len(names)):
    print(f"{i+1}. {names[i]}")
```

---

## 四、enumerate() 与 zip()

### 4.1 enumerate() - 同时获取索引和元素

```python
fruits = ["apple", "banana", "cherry"]

# 同时获取索引和值
for index, fruit in enumerate(fruits):
    print(f"{index}: {fruit}")
# 0: apple
# 1: banana
# 2: cherry

# 指定起始索引
for index, fruit in enumerate(fruits, start=1):
    print(f"{index}. {fruit}")
# 1. apple
# 2. banana
# 3. cherry
```

### 4.2 zip() - 并行遍历多个序列

```python
names = ["Alice", "Bob", "Charlie"]
ages = [25, 30, 35]
cities = ["北京", "上海", "广州"]

# 同时遍历多个列表
for name, age, city in zip(names, ages, cities):
    print(f"{name}, {age}岁, 来自{city}")

# zip以最短序列为准
list1 = [1, 2, 3, 4]
list2 = ["a", "b"]
for x, y in zip(list1, list2):
    print(x, y)  # 只输出两组: (1,a) (2,b)

# zip与enumerate结合
for i, (name, age) in enumerate(zip(names, ages)):
    print(f"{i}: {name} is {age} years old")
```

---

## 五、列表推导式

### 5.1 基本语法

```python
# [表达式 for 变量 in 可迭代对象 if 条件]

# 等价于:
# result = []
# for 变量 in 可迭代对象:
#     if 条件:
#         result.append(表达式)
```

### 5.2 各种示例

```python
# 平方数列表
squares = [x**2 for x in range(1, 6)]
# [1, 4, 9, 16, 25]

# 带条件的列表推导
even_squares = [x**2 for x in range(10) if x % 2 == 0]
# [0, 4, 16, 36, 64]

# 处理字符串
words = ["Hello", "World", "Python"]
lengths = [len(word) for word in words]
# [5, 5, 6]

# 嵌套列表推导（矩阵转置）
matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
transposed = [[row[i] for row in matrix] for i in range(3)]
# [[1, 4, 7], [2, 5, 8], [3, 6, 9]]
```

### 5.3 字典和集合推导式

```python
# 字典推导
words = ["apple", "banana", "cherry"]
word_lengths = {word: len(word) for word in words}
# {'apple': 5, 'banana': 6, 'cherry': 6}

# 集合推导
numbers = [1, 2, 2, 3, 3, 3]
unique_squares = {x**2 for x in numbers}
# {1, 4, 9}
```

---

## 六、嵌套循环

### 6.1 基础嵌套

```python
# 打印乘法表
for i in range(1, 10):
    for j in range(1, i + 1):
        print(f"{j}×{i}={i*j:2}", end="  ")
    print()  # 换行
```

### 6.2 遍历二维结构

```python
matrix = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
]

# 遍历每个元素
for row in matrix:
    for num in row:
        print(num, end=" ")
    print()

# 计算每行和
for i, row in enumerate(matrix):
    row_sum = sum(row)
    print(f"第{i+1}行和: {row_sum}")
```

### 6.3 控制嵌套循环

```python
# 找第一个满足条件的元素
matrix = [[1, 2], [3, 4], [5, 6]]
found = False

for i, row in enumerate(matrix):
    for j, num in enumerate(row):
        if num > 3:
            print(f"找到: matrix[{i}][{j}] = {num}")
            found = True
            break  # 跳出内层循环
    if found:
        break  # 跳出外层循环
```

---

## 七、循环控制：break / continue / else

### 7.1 break - 终止循环

```python
# 找到第一个偶数就停止
numbers = [1, 3, 5, 8, 9, 10]
for num in numbers:
    if num % 2 == 0:
        print(f"第一个偶数: {num}")
        break
else:
    print("没有找到偶数")  # 如果break执行了，这里不会执行
```

### 7.2 continue - 跳过当前迭代

```python
# 打印奇数，跳过偶数
for i in range(10):
    if i % 2 == 0:
        continue  # 跳过偶数，直接进行下一次循环
    print(i)  # 只输出: 1, 3, 5, 7, 9
```

### 7.3 for-else 结构

```python
# for循环正常结束（没有被break）时执行else
for n in range(2, 10):
    for x in range(2, n):
        if n % x == 0:
            print(f"{n} = {x} × {n//x}")
            break
    else:  # 没有触发break时执行（n是质数）
        print(f"{n} 是质数")
```

### 7.4 对比表

| 语句 | 作用 | 使用场景 |
|:-----|:-----|:---------|
| `break` | 立即终止整个循环 | 找到目标后提前退出 |
| `continue` | 跳过当前迭代，继续下一次 | 过滤某些条件 |
| `else` | 循环正常结束时执行 | 处理"未找到"的情况 |

---

## 八、迭代器原理

### 8.1 可迭代对象 vs 迭代器

```python
# 可迭代对象（Iterable）
my_list = [1, 2, 3]
print(iter(my_list))  # <list_iterator object>

# 迭代器（Iterator）
iterator = iter(my_list)
print(next(iterator))  # 1
print(next(iterator))  # 2
print(next(iterator))  # 3
# print(next(iterator))  # StopIteration 异常
```

### 8.2 for循环的工作原理

```python
# for x in iterable:
#     do_something(x)

# 实际等价于:
# iterator = iter(iterable)
# while True:
#     try:
#         x = next(iterator)
#         do_something(x)
#     except StopIteration:
#         break
```

### 8.3 自定义迭代器

```python
class CountDown:
    def __init__(self, start):
        self.start = start
    
    def __iter__(self):
        return self
    
    def __next__(self):
        if self.start <= 0:
            raise StopIteration
        self.start -= 1
        return self.start + 1

# 使用
for num in CountDown(5):
    print(num)  # 5, 4, 3, 2, 1
```

---

## 九、常见错误与调试

### 9.1 经典错误

```python
# ❌ 错误1：修改正在遍历的列表
numbers = [1, 2, 3, 4, 5]
for num in numbers:
    if num % 2 == 0:
        numbers.remove(num)  # 危险！会导致跳过某些元素

# ✅ 正确做法：遍历副本
for num in numbers[:]:
    if num % 2 == 0:
        numbers.remove(num)

# 或者使用列表推导
numbers = [num for num in numbers if num % 2 != 0]
```

```python
# ❌ 错误2：混淆range的边界
for i in range(5):  # 是 0-4，不是 0-5
    print(i)

# ❌ 错误3：缩进错误
for i in range(3):
print(i)  # IndentationError
```

```python
# ❌ 错误4：变量未定义就在循环外使用
for i in range(3):
    x = i * 2
print(x)  # x在这里有值，但不推荐这样做

# ✅ 更好的做法：初始化变量
x = 0
for i in range(3):
    x = i * 2
print(x)
```

### 9.2 调试技巧

```python
# 使用print跟踪循环过程
for i in range(5):
    print(f"DEBUG: i = {i}")
    # 处理逻辑

# 使用pdb调试
import pdb
for item in data:
    if item > 100:
        pdb.set_trace()  # 在此处打断点
        # 检查变量值
```

---

## 十、实战练习

### 练习1：统计字符
统计字符串中每个字符出现的次数。

```python
def count_chars(s):
    result = {}
    for char in s:
        if char in result:
            result[char] += 1
        else:
            result[char] = 1
    return result

# 或使用字典推导简化
from collections import Counter
# Counter("hello")  # {'h': 1, 'e': 1, 'l': 2, 'o': 1}
```

### 练习2：找素数
找出2-100之间的所有素数。

```python
def find_primes(start, end):
    primes = []
    for num in range(start, end + 1):
        if num < 2:
            continue
        for i in range(2, int(num**0.5) + 1):
            if num % i == 0:
                break
        else:
            primes.append(num)
    return primes

print(find_primes(2, 100))
```

### 练习3：扁平化列表
将嵌套列表扁平化。

```python
def flatten(nested_list):
    result = []
    for item in nested_list:
        if isinstance(item, list):
            result.extend(flatten(item))  # 递归
        else:
            result.append(item)
    return result

nested = [1, [2, 3], [4, [5, 6]], 7]
print(flatten(nested))  # [1, 2, 3, 4, 5, 6, 7]
```

### 练习4：矩阵乘法
实现两个矩阵相乘。

```python
def matrix_multiply(A, B):
    rows_A = len(A)
    cols_A = len(A[0])
    cols_B = len(B[0])
    
    # 初始化结果矩阵
    result = [[0 for _ in range(cols_B)] for _ in range(rows_A)]
    
    for i in range(rows_A):
        for j in range(cols_B):
            for k in range(cols_A):
                result[i][j] += A[i][k] * B[k][j]
    
    return result
```

---

## 速查表

| 用法 | 代码示例 |
|:-----|:---------|
| 基础遍历 | `for x in list: print(x)` |
| 带索引 | `for i, x in enumerate(list):` |
| 多序列 | `for a, b in zip(list1, list2):` |
| 数值范围 | `for i in range(10):` |
| 倒序 | `for i in range(10, 0, -1):` |
| 列表推导 | `[x*2 for x in list]` |
| 字典推导 | `{k: v for k, v in zip(keys, values)}` |
| 跳过元素 | `if x < 0: continue` |
| 提前退出 | `if x == target: break` |

---

> **总结**：Python 的 for 循环设计简洁，配合 `range()`、`enumerate()`、`zip()` 等内置函数，以及列表推导式，可以优雅地处理各种遍历场景。