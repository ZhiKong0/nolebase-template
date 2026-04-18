"""
实验三：Python内置数据结构&算法性能分析
湖北经济学院
"""


# ==================== 任务一：列表内置操作 ====================
def task1_list_operations():
    """任务一：Python列表的内置应用操作"""
    print("=" * 60)
    print("任务一：Python列表内置操作练习")
    print("=" * 60)
    
    # 创建列表
    A = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
    print(f"\n1. 初始列表 A = {A}")
    
    # 1. append操作
    print("\n--- append 操作 ---")
    A.append(10)
    print(f"A.append(10) 后: A = {A}")
    print("解释: append() 在列表末尾添加一个新元素")
    
    # 2. insert操作
    print("\n--- insert 操作 ---")
    A.insert(0, -1)
    print(f"A.insert(0, -1) 后: A = {A}")
    print("解释: insert(i, item) 在列表的第i个位置插入一个元素")
    
    # 3. pop操作（无参数）
    print("\n--- pop 操作 ---")
    popped_item = A.pop()
    print(f"A.pop() 返回: {popped_item}, 列表变为: A = {A}")
    print("解释: pop() 删除并返回列表中最后一个元素")
    
    # 4. pop(i)操作
    print("\n--- pop(i) 操作 ---")
    popped_item_i = A.pop(0)
    print(f"A.pop(0) 返回: {popped_item_i}, 列表变为: A = {A}")
    print("解释: pop(i) 删除并返回列表中第i个位置的元素")
    
    # 5. sort操作
    print("\n--- sort 操作 ---")
    B = [3, 1, 4, 1, 5, 9, 2, 6]
    print(f"排序前: B = {B}")
    B.sort()
    print(f"B.sort() 排序后: B = {B}")
    print("解释: sort() 将列表元素按升序排序")
    
    # 6. reverse操作
    print("\n--- reverse 操作 ---")
    B.reverse()
    print(f"B.reverse() 倒序后: B = {B}")
    print("解释: reverse() 将列表元素倒序排列")
    
    # 7. del操作
    print("\n--- del 操作 ---")
    C = [10, 20, 30, 40, 50]
    print(f"操作前: C = {C}")
    del C[2]
    print(f"del C[2] 后: C = {C}")
    print("解释: del alist[i] 删除列表中第i个位置的元素")
    
    # 8. index操作
    print("\n--- index 操作 ---")
    D = ['a', 'b', 'c', 'd', 'b']
    print(f"D = {D}")
    idx = D.index('b')
    print(f"D.index('b') 返回: {idx}")
    print("解释: index(item) 返回item第一次出现时的下标")
    
    # 9. count操作
    print("\n--- count 操作 ---")
    print(f"D = {D}")
    cnt = D.count('b')
    print(f"D.count('b') 返回: {cnt}")
    print("解释: count(item) 返回item在列表中出现的次数")
    
    # 10. remove操作
    print("\n--- remove 操作 ---")
    print(f"操作前: D = {D}")
    D.remove('b')
    print(f"D.remove('b') 后: D = {D}")
    print("解释: remove(item) 从列表中移除第一次出现的item")


# ==================== 任务二：异序词检测1 - 清点法 ====================
def anagramSolution1(s1, s2):
    """清点法 - 时间复杂度 O(n log n)"""
    if len(s1) != len(s2):
        return False
    return sorted(s1) == sorted(s2)


def task2_anagram_solution1():
    """任务二：异序词检测1 - 清点法"""
    print("\n" + "=" * 60)
    print("任务二：异序词检测1 - 清点法")
    print("=" * 60)
    
    test_cases = [
        ("abcdefg", "gfedcba"),
        ("python", "typhon"),
        ("listen", "silent"),
        ("hello", "world"),
        ("algorithm", "logarithm"),
        ("anagram", "nagaram"),
        ("conversation", "voicesranton"),
    ]
    
    print("\n测试结果:")
    print("-" * 60)
    for s1, s2 in test_cases:
        result = anagramSolution1(s1, s2)
        status = "是异序词" if result else "不是异序词"
        print(f"'{s1}' vs '{s2}': {status} (长度: {len(s1)})")
    print("-" * 60)
    
    print("\n时间复杂度分析:")
    print("  总时间复杂度: O(n log n)")
    print("  空间复杂度: O(n)")


# ==================== 任务三：异序词检测2 - 计数比较法 ====================
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


def task3_anagram_solution4():
    """任务三：异序词检测2 - 计数比较法"""
    print("\n" + "=" * 60)
    print("任务三：异序词检测2 - 计数比较法")
    print("=" * 60)
    
    test_cases = [
        ("abcdefg", "gfedcba"),
        ("python", "typhon"),
        ("listen", "silent"),
        ("hello", "world"),
        ("algorithm", "logarithm"),
        ("anagram", "nagaram"),
    ]
    
    print("\n测试结果:")
    print("-" * 60)
    for s1, s2 in test_cases:
        result = anagramSolution4(s1.lower(), s2.lower())
        status = "是异序词" if result else "不是异序词"
        print(f"'{s1}' vs '{s2}': {status}")
    print("-" * 60)
    
    print("\n时间复杂度分析:")
    print("  总时间复杂度: O(n)")
    print("  空间复杂度: O(1)")


# ==================== 任务四：算法性能计时分析 ====================
def task4_performance_timing():
    """任务四（选做）：算法性能计时分析"""
    import timeit
    import random
    import string
    
    print("\n" + "=" * 60)
    print("任务四（选做）：算法性能计时分析")
    print("=" * 60)
    
    def generate_random_string(length):
        return ''.join(random.choice(string.ascii_lowercase) for _ in range(length))
    
    lengths = [10, 50, 100, 500, 1000]
    
    print("\n运行时间对比（单位：秒）:")
    print("-" * 60)
    print(f"{'字符串长度':<12} {'清点法 O(n log n)':<20} {'计数比较法 O(n)':<20}")
    print("-" * 60)
    
    for length in lengths:
        test_s1 = generate_random_string(length)
        test_s2 = generate_random_string(length)
        repeat = 1000
        
        time1 = timeit.timeit(lambda: anagramSolution1(test_s1, test_s2), number=repeat) / repeat
        time2 = timeit.timeit(lambda: anagramSolution4(test_s1, test_s2), number=repeat) / repeat
        
        print(f"{length:<12} {time1:<20.8f} {time2:<20.8f}")
    
    print("-" * 60)
    print("\n结论: 计数比较法(O(n))在大规模数据下性能更优")


# ==================== 主程序 ====================
if __name__ == "__main__":
    print("=" * 60)
    print("实验三：Python内置数据结构&算法性能分析")
    print("湖北经济学院")
    print("=" * 60)
    
    # 任务一：列表内置操作
    task1_list_operations()
    
    # 任务二：异序词检测1 - 清点法
    task2_anagram_solution1()
    
    # 任务三：异序词检测2 - 计数比较法
    task3_anagram_solution4()
    
    # 任务四（选做）：算法性能计时分析
    task4_performance_timing()
    
    print("\n" + "=" * 60)
    print("实验完成！")
    print("=" * 60)
