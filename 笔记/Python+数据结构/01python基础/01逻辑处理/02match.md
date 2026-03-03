# Python match-case 模式匹配详解

> Python 3.10+ 引入的 `match-case` 语法，提供更强大的结构化数据匹配能力，比传统的 if-elif 链更清晰优雅。

---

## 目录

1. [基础语法](#一基础语法)
2. [字面量模式](#二字面量模式)
3. [变量模式与通配符](#三变量模式与通配符)
4. [序列模式](#四序列模式)
5. [映射模式（字典匹配）](#五映射模式字典匹配)
6. [类模式](#六类模式)
7. [守卫子句（Guard）](#七守卫子句guard)
8. [或模式（OR Pattern）](#八或模式or-pattern)
9. [match vs if-elif 对比](#九match-vs-if-elif-对比)
10. [实战练习](#十实战练习)

---

## 一、基础语法

### 1.1 基本结构

```python
match 表达式:
    case 模式1:
        # 匹配模式1时执行
        语句块
    case 模式2:
        # 匹配模式2时执行
        语句块
    case _:
        # 默认情况（类似于switch的default）
        语句块
```

### 1.2 最简单的示例

```python
def http_status(status):
    match status:
        case 200:
            return "OK"
        case 404:
            return "Not Found"
        case 500:
            return "Server Error"
        case _:
            return "Unknown Status"

# 测试
print(http_status(200))   # OK
print(http_status(404))   # Not Found
print(http_status(999))   # Unknown Status
```

---

## 二、字面量模式

### 2.1 基础字面量匹配

```python
def describe_number(n):
    match n:
        case 0:
            return "零"
        case 1:
            return "一"
        case 2:
            return "二"
        case _:
            return "其他数字"

# 支持多种类型
match value:
    case 42:           # 整数
        print("是42")
    case "hello":      # 字符串
        print("是hello")
    case True:         # 布尔值
        print("是True")
    case None:         # None
        print("是None")
```

### 2.2 与元组结合

```python
def describe_point(point):
    match point:
        case (0, 0):
            return "原点"
        case (x, 0):     # 变量模式捕获
            return f"在x轴上，x={x}"
        case (0, y):     # 变量模式捕获
            return f"在y轴上，y={y}"
        case (x, y):     # 任意点
            return f"点({x}, {y})"
        case _:
            return "不是有效的点"

# 测试
print(describe_point((0, 0)))      # 原点
print(describe_point((5, 0)))      # 在x轴上，x=5
print(describe_point((3, 4)))      # 点(3, 4)
```

---

## 三、变量模式与通配符

### 3.1 变量绑定

```python
def analyze_value(value):
    match value:
        case 0:
            print("是零")
        case x:           # 绑定到变量x
            print(f"值是: {x}")

analyze_value(42)  # 值是: 42
```

### 3.2 通配符 `_`

```python
def check_list(lst):
    match lst:
        case []:
            return "空列表"
        case [single]:   # 单元素
            return f"一个元素: {single}"
        case [first, *_]:  # 用_忽略剩余元素
            return f"多个元素，第一个是: {first}"
```

### 3.3 忽略特定值

```python
def process_tuple(t):
    match t:
        case (_, 0, _):   # 只关心第二个值是0
            return "第二个元素为0"
        case (1, _, _):
            return "第一个元素为1"
        case _:
            return "其他情况"
```

---

## 四、序列模式

### 4.1 列表匹配

```python
def analyze_list(data):
    match data:
        case []:
            return "空列表"
        case [single]:
            return f"单元素: {single}"
        case [first, second]:
            return f"两个元素: {first}, {second}"
        case [first, *rest]:
            return f"首元素: {first}, 其余: {rest}"
        case _:
            return "其他"

# 测试
print(analyze_list([]))              # 空列表
print(analyze_list([42]))            # 单元素: 42
print(analyze_list([1, 2]))          # 两个元素: 1, 2
print(analyze_list([1, 2, 3, 4]))    # 首元素: 1, 其余: [2, 3, 4]
```

### 4.2 嵌套序列匹配

```python
def analyze_matrix_element(data):
    match data:
        case [[x, y], [z, w]]:  # 2x2矩阵
            return f"2x2矩阵: [[{x},{y}],[{z},{w}]]"
        case [[_, _], [_, _]]:  # 不关心具体值
            return "2x2矩阵(值不关心)"
        case [[first_row], [second_row]]:
            return f"2x1矩阵: 第一行{first_row}, 第二行{second_row}"
        case _:
            return "不是预期的矩阵格式"

print(analyze_matrix_element([[1, 2], [3, 4]]))
# 2x2矩阵: [[1,2],[3,4]]
```

### 4.3 星号表达式

```python
def process_command(args):
    match args:
        case ["copy", source, destination]:
            return f"复制 {source} 到 {destination}"
        case ["move", source, destination]:
            return f"移动 {source} 到 {destination}"
        case ["delete", *files]:  # 捕获任意数量的文件
            return f"删除文件: {files}"
        case _:
            return "未知命令"

print(process_command(["copy", "a.txt", "b.txt"]))
# 复制 a.txt 到 b.txt

print(process_command(["delete", "a.txt", "b.txt", "c.txt"]))
# 删除文件: ['a.txt', 'b.txt', 'c.txt']
```

---

## 五、映射模式（字典匹配）

### 5.1 基础字典匹配

```python
def handle_response(response):
    match response:
        case {"status": 200, "data": data}:
            return f"成功，数据: {data}"
        case {"status": 404}:
            return "未找到"
        case {"status": code}:
            return f"状态码: {code}"
        case {}:
            return "空响应"
        case _:
            return "不是字典"

# 测试
print(handle_response({"status": 200, "data": "hello"}))
# 成功，数据: hello

print(handle_response({"status": 500}))
# 状态码: 500
```

### 5.2 部分匹配（多余键不影响）

```python
def analyze_user(user):
    match user:
        case {"name": str(name), "age": int(age)}:
            return f"用户: {name}, {age}岁"
        case {"name": name}:  # 只匹配name字段
            return f"用户名: {name}"
        case {"age": age}:
            return f"年龄: {age}"
        case _:
            return "不是有效的用户信息"

# 多余的字段不影响匹配
user = {"name": "张三", "age": 25, "city": "北京", "email": "zhang@example.com"}
print(analyze_user(user))  # 用户: 张三, 25岁
```

### 5.3 双星号捕获剩余键

```python
def extract_user_info(user):
    match user:
        case {"id": user_id, "name": name, **rest}:
            return f"ID: {user_id}, 姓名: {name}, 其他: {rest}"
        case _:
            return "格式不符"

user = {"id": 1, "name": "李四", "age": 30, "city": "上海"}
print(extract_user_info(user))
# ID: 1, 姓名: 李四, 其他: {'age': 30, 'city': '上海'}
```

---

## 六、类模式

### 6.1 匹配类实例

```python
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

def describe_point(p):
    match p:
        case Point(x=0, y=0):
            return "原点"
        case Point(x=0, y=y):
            return f"Y轴上，y={y}"
        case Point(x=x, y=0):
            return f"X轴上，x={x}"
        case Point(x=x, y=y) if x == y:
            return f"对角线上，x=y={x}"
        case Point(x=x, y=y):
            return f"点({x}, {y})"
        case _:
            return "不是Point对象"

# 测试
print(describe_point(Point(0, 0)))   # 原点
print(describe_point(Point(3, 0)))   # X轴上，x=3
print(describe_point(Point(2, 2)))   # 对角线上，x=y=2
```

### 6.2 匹配dataclass

```python
from dataclasses import dataclass

@dataclass
class User:
    name: str
    age: int
    email: str = None

def process_user(user):
    match user:
        case User(name=name, age=age) if age < 18:
            return f"未成年用户: {name}"
        case User(name=name, age=age) if age >= 60:
            return f"老年用户: {name}"
        case User(name=name, email=None):
            return f"未绑定邮箱: {name}"
        case User(name=name, email=email):
            return f"普通用户: {name} ({email})"
        case _:
            return "未知用户类型"

# 测试
print(process_user(User("小明", 15)))
# 未成年用户: 小明

print(process_user(User("老王", 65)))
# 老年用户: 老王

print(process_user(User("张三", 30, "zhang@example.com")))
# 普通用户: 张三 (zhang@example.com)
```

### 6.3 嵌套类匹配

```python
@dataclass
class Address:
    city: str
    country: str

@dataclass
class Person:
    name: str
    address: Address

def describe_person_location(person):
    match person:
        case Person(name=name, address=Address(city="北京", country="中国")):
            return f"{name} 住在中国北京"
        case Person(name=name, address=Address(country="中国")):
            return f"{name} 住在中国其他城市"
        case Person(name=name, address=Address(city=city, country=country)):
            return f"{name} 住在 {country} 的 {city}"
        case _:
            return "未知"

person = Person("李四", Address("上海", "中国"))
print(describe_person_location(person))
# 李四 住在中国其他城市
```

---

## 七、守卫子句（Guard）

### 7.1 if 条件过滤

```python
def classify_number(n):
    match n:
        case x if x < 0:
            return f"负数: {x}"
        case x if x == 0:
            return "零"
        case x if 0 < x < 10:
            return f"个位数: {x}"
        case x if 10 <= x < 100:
            return f"两位数: {x}"
        case x if x >= 100:
            return f"大数: {x}"

print(classify_number(-5))   # 负数: -5
print(classify_number(5))    # 个位数: 5
print(classify_number(50))   # 两位数: 50
```

### 7.2 复杂守卫条件

```python
def validate_age(age_info):
    match age_info:
        case {"age": int(age)} if 0 <= age <= 120:
            return f"有效年龄: {age}"
        case {"age": int(age)} if age < 0:
            return f"无效: 年龄不能为负数 ({age})"
        case {"age": int(age)}:
            return f"无效: 年龄过大 ({age})"
        case {"age": _}:
            return "无效: 年龄必须是整数"
        case _:
            return "无效: 缺少年龄字段"

print(validate_age({"age": 25}))
# 有效年龄: 25

print(validate_age({"age": -5}))
# 无效: 年龄不能为负数 (-5)

print(validate_age({"age": "未知"}))
# 无效: 年龄必须是整数
```

---

## 八、或模式（OR Pattern）

### 8.1 多模式匹配

```python
def describe_day(day):
    match day:
        case "Saturday" | "Sunday":
            return "周末"
        case "Monday" | "Tuesday" | "Wednesday" | "Thursday" | "Friday":
            return "工作日"
        case _:
            return "无效的星期"

print(describe_day("Saturday"))  # 周末
print(describe_day("Monday"))    # 工作日
```

### 8.2 带捕获的或模式

```python
def handle_error(code):
    match code:
        case 400 | 401 | 403 | 404 as err:
            return f"客户端错误: {err}"
        case 500 | 502 | 503 | 504 as err:
            return f"服务器错误: {err}"
        case _:
            return f"其他错误: {code}"

print(handle_error(404))  # 客户端错误: 404
print(handle_error(503))  # 服务器错误: 503
```

---

## 九、match vs if-elif 对比

### 9.1 场景对比

```python
# 场景1: HTTP状态码处理
# if-elif 写法
status = 404
if status == 200:
    msg = "OK"
elif status == 404:
    msg = "Not Found"
elif status == 500:
    msg = "Server Error"
else:
    msg = "Unknown"

# match-case 写法（更简洁，意图更明确）
match status:
    case 200:
        msg = "OK"
    case 404:
        msg = "Not Found"
    case 500:
        msg = "Server Error"
    case _:
        msg = "Unknown"
```

```python
# 场景2: 数据结构解构
# if-elif 写法（繁琐）
data = {"type": "user", "name": "张三", "age": 25}
if isinstance(data, dict) and data.get("type") == "user":
    name = data.get("name")
    age = data.get("age")
    if name and age:
        result = f"用户 {name}, {age}岁"

# match-case 写法（优雅）
match data:
    case {"type": "user", "name": str(name), "age": int(age)}:
        result = f"用户 {name}, {age}岁"
    case _:
        result = "格式错误"
```

### 9.2 对比表

| 特性 | if-elif | match-case |
|:-----|:--------|:-----------|
| 字面量匹配 | ✅ 支持 | ✅ 支持 |
| 数据结构解构 | ❌ 手动实现 | ✅ 原生支持 |
| 类型匹配 | ❌ 需isinstance | ✅ case内置 |
| 嵌套匹配 | ❌ 繁琐 | ✅ 简洁优雅 |
| 守卫条件 | ✅ 直接写if | ✅ 支持if守卫 |
| 性能 | ⚡ 略快 | ⚡ 3.10+优化后接近 |
| 兼容性 | ✅ 全版本 | ⚠️ 仅3.10+ |

---

## 十、实战练习

### 练习1：简化版表达式求值

```python
def evaluate(expr):
    """简化版的表达式求值器"""
    match expr:
        case int(x) | float(x):  # 字面量
            return x
        case {"+": [a, b]}:  # 加法
            return evaluate(a) + evaluate(b)
        case {"-": [a, b]}:  # 减法
            return evaluate(a) - evaluate(b)
        case {"*": [a, b]}:  # 乘法
            return evaluate(a) * evaluate(b)
        case {"/": [a, b]}:  # 除法
            return evaluate(a) / evaluate(b)
        case _:
            raise ValueError(f"未知表达式: {expr}")

# 测试
print(evaluate(42))                      # 42
print(evaluate({"+": [1, 2]}))            # 3
print(evaluate({"*": [{"+": [1, 2]}, 3]}))  # 9 ((1+2)*3)
```

### 练习2：处理不同形状

```python
from dataclasses import dataclass
from math import pi

@dataclass
class Circle:
    radius: float

@dataclass
class Rectangle:
    width: float
    height: float

@dataclass
class Triangle:
    base: float
    height: float

def calculate_area(shape):
    match shape:
        case Circle(radius=r):
            return pi * r ** 2
        case Rectangle(width=w, height=h):
            return w * h
        case Triangle(base=b, height=h):
            return 0.5 * b * h
        case _:
            raise ValueError(f"未知形状: {shape}")

# 测试
print(calculate_area(Circle(5)))
# 78.53981633974483

print(calculate_area(Rectangle(4, 5)))
# 20

print(calculate_area(Triangle(6, 4)))
# 12.0
```

### 练习3：处理命令行参数

```python
def parse_command(args):
    """解析命令行参数"""
    match args:
        case []:
            return {"cmd": "help"}
        case ["help"]:
            return {"cmd": "help"}
        case ["version"]:
            return {"cmd": "version"}
        case ["copy", source, dest]:
            return {"cmd": "copy", "source": source, "dest": dest}
        case ["move", source, dest]:
            return {"cmd": "move", "source": source, "dest": dest}
        case ["delete", *files] if files:
            return {"cmd": "delete", "files": files}
        case _:
            return {"cmd": "unknown", "args": args}

# 测试
print(parse_command([]))
# {'cmd': 'help'}

print(parse_command(["copy", "a.txt", "b.txt"]))
# {'cmd': 'copy', 'source': 'a.txt', 'dest': 'b.txt'}

print(parse_command(["delete", "x.txt", "y.txt", "z.txt"]))
# {'cmd': 'delete', 'files': ['x.txt', 'y.txt', 'z.txt']}
```

### 练习4：处理JSON API响应

```python
def process_api_response(response):
    """处理API响应，返回统一格式"""
    match response:
        # 成功响应
        case {"status": "success", "data": data}:
            return {"ok": True, "result": data}
        
        # 错误响应 - 带错误详情
        case {"status": "error", "code": int(code), "message": msg}:
            return {"ok": False, "error": msg, "code": code}
        
        # 错误响应 - 简化版
        case {"status": "error", "message": msg}:
            return {"ok": False, "error": msg, "code": None}
        
        # 分页响应
        case {"status": "success", "data": list(data), "page": page, "total": total}:
            return {
                "ok": True,
                "result": data,
                "pagination": {"page": page, "total": total}
            }
        
        # 未知格式
        case _:
            return {"ok": False, "error": "Unknown response format"}

# 测试
print(process_api_response({"status": "success", "data": "hello"}))
# {'ok': True, 'result': 'hello'}

print(process_api_response({"status": "error", "code": 404, "message": "Not found"}))
# {'ok': False, 'error': 'Not found', 'code': 404}
```

---

## 速查表

| 用法 | 代码示例 |
|:-----|:---------|
| 字面量匹配 | `case 42: ...` |
| 变量绑定 | `case x: ...` |
| 通配符 | `case _: ...` |
| 序列匹配 | `case [a, b]: ...` |
| 序列（任意长度） | `case [first, *rest]: ...` |
| 字典匹配 | `case {"key": value}: ...` |
| 捕获剩余键 | `case {"id": id, **rest}: ...` |
| 类模式 | `case Point(x=x, y=y): ...` |
| 守卫子句 | `case x if x > 0: ...` |
| 或模式 | `case "A" \| "B": ...` |

---

## 注意事项

1. **Python版本**: `match-case` 需要 Python 3.10+
2. **执行顺序**: 从上到下匹配，第一个匹配的 case 执行后跳出
3. **无贯穿**: 不需要 `break`，执行完自动跳出
4. **变量作用域**: case 中绑定的变量在 match 块内可用

---

> **总结**: `match-case` 是处理结构化数据的利器，特别适合解析JSON、处理AST、实现状态机等场景。相比 if-elif 链，代码意图更清晰，解构赋值更优雅。
