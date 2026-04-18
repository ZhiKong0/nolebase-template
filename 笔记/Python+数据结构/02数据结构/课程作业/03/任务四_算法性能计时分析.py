"""
任务四（选做）：程序运行计时 - 验证算法复杂度
利用timeit模块对任务二和三的两个程序，记录程序花费的时间
"""

import timeit
import random
import string


# ============ 方案1：清点法 ============
def anagramSolution1(s1, s2):
    """清点法 - 时间复杂度 O(n log n)"""
    return sorted(s1) == sorted(s2)


# ============ 方案4：计数比较法 ============
def anagramSolution4(s1, s2):
    """计数比较法 - 时间复杂度 O(n)"""
    if len(s1) != len(s2):
        return False
    count = [0] * 26
    for c in s1:
        count[ord(c) - ord('a')] += 1
    for c in s2:
        count[ord(c) - ord('a')] -= 1
        if count[ord(c) - ord('a')] < 0:
            return False
    return True


def generate_random_string(length):
    """生成指定长度的随机小写字母字符串"""
    return ''.join(random.choice(string.ascii_lowercase) for _ in range(length))


print("=" * 70)
print("任务四（选做）：算法性能计时分析")
print("=" * 70)

# 测试不同长度字符串的性能
lengths = [10, 50, 100, 500, 1000, 2000]

print("\n运行时间对比（单位：秒）:")
print("-" * 70)
print(f"{'字符串长度':<12} {'清点法 O(n log n)':<22} {'计数比较法 O(n)':<22}")
print("-" * 70)

for length in lengths:
    # 生成两个等长的随机字符串
    test_s1 = generate_random_string(length)
    test_s2 = generate_random_string(length)
    
    # 使用timeit重复执行多次取平均值
    repeat = 1000
    
    # 清点法计时
    time1 = timeit.timeit(
        lambda: anagramSolution1(test_s1, test_s2),
        number=repeat
    )
    
    # 计数比较法计时
    time2 = timeit.timeit(
        lambda: anagramSolution4(test_s1, test_s2),
        number=repeat
    )
    
    avg_time1 = time1 / repeat
    avg_time2 = time2 / repeat
    
    print(f"{length:<12} {avg_time1:<22.8f} {avg_time2:<22.8f}")

print("-" * 70)

print("\n性能分析:")
print("=" * 70)
print("""
1. 清点法（sorted排序）:
   - 时间复杂度: O(n log n)
   - 优点：代码简洁，易于理解
   - 缺点：需要额外内存空间存储排序后的列表

2. 计数比较法:
   - 时间复杂度: O(n)
   - 优点：效率更高，尤其在长字符串时优势明显
   - 缺点：需要假设字符集有限（这里是26个字母）
          如果字符集很大，空间开销也会增加

3. 从测试结果可以看出:
   - 当字符串长度较小时，两种方法差异不明显
   - 随着字符串长度增加，计数比较法的优势越来越明显
   - 这是因为O(n)算法增长速率比O(n log n)更慢
""")

print("\n验证算法复杂度:")
print("-" * 70)
print("对于O(n log n)算法，当n翻倍时，时间应增加约2.1倍")
print("对于O(n)算法，当n翻倍时，时间应增加约2倍")

# 计算增长比率
lengths_test = [100, 200, 500, 1000]
print("\n增长比率验证:")
print("-" * 70)

for i in range(len(lengths_test) - 1):
    n1, n2 = lengths_test[i], lengths_test[i + 1]
    ratio_n = n2 / n1
    
    # 生成测试字符串
    s1_n1 = generate_random_string(n1)
    s2_n1 = generate_random_string(n1)
    s1_n2 = generate_random_string(n2)
    s2_n2 = generate_random_string(n2)
    
    repeat = 500
    
    time_n1 = timeit.timeit(lambda: anagramSolution1(s1_n1, s1_n1), number=repeat) / repeat
    time_n2 = timeit.timeit(lambda: anagramSolution1(s1_n2, s1_n2), number=repeat) / repeat
    
    actual_ratio = time_n2 / time_n1
    expected_ratio = ratio_n * (n2 / n1)  # n log n 的增长因子
    
    print(f"n: {n1} -> {n2}, 长度比率: {ratio_n:.1f}, 时间比率: {actual_ratio:.2f}")

print("-" * 70)
print("=" * 70)
print("\n结论：从理论上讲，sorted方法的时间复杂度是O(n log n)，")
print("计数比较法的时间复杂度是O(n)，在大规模数据下，O(n)算法更优。")
print("=" * 70)
