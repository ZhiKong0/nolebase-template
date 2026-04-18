"""
任务三：异序词检测2 - 方案4：计数比较法
教材P50页 代码2-8
时间复杂度: O(n)
"""


def anagramSolution4(s1, s2):
    """
    计数比较法：统计两个字符串中每个字符出现的次数
    
    算法步骤：
    1. 创建两个长度为26的计数器列表（假设只有小写字母）
    2. 遍历第一个字符串，统计每个字符出现的次数
    3. 遍历第二个字符串，统计每个字符出现的次数
    4. 比较两个计数器列表是否完全相同
    
    时间复杂度: O(n)
    空间复杂度: O(1) - 固定大小的计数器列表
    """
    # 创建26个字母的计数器，初始化为0
    c1 = [0] * 26
    c2 = [0] * 26
    
    # 遍历第一个字符串，统计字符出现次数
    for i in range(len(s1)):
        # ord(c) 获取字符的ASCII码
        # ord('a') = 97，所以位置索引 = ord(c) - ord('a')
        pos = ord(s1[i]) - ord('a')
        c1[pos] += 1
    
    # 遍历第二个字符串，统计字符出现次数
    for i in range(len(s2)):
        pos = ord(s2[i]) - ord('a')
        c2[pos] += 1
    
    # 比较两个计数器列表
    stillOK = True
    j = 0
    while j < 26 and stillOK:
        if c1[j] == c2[j]:
            j += 1
        else:
            stillOK = False
    
    return stillOK


def anagramSolution4_v2(s1, s2):
    """
    计数比较法改进版：使用Python的count方法
    
    时间复杂度: O(n)
    空间复杂度: O(1)
    """
    if len(s1) != len(s2):
        return False
    
    # 统计26个字母出现的次数
    count = [0] * 26
    
    for c in s1:
        count[ord(c) - ord('a')] += 1
    
    for c in s2:
        count[ord(c) - ord('a')] -= 1
        if count[ord(c) - ord('a')] < 0:
            return False
    
    return True


# 测试案例
print("=" * 60)
print("任务三：异序词检测2 - 计数比较法")
print("=" * 60)

test_cases = [
    ("abcdefg", "gfedcba"),        # 异序词
    ("python", "typhon"),          # 异序词
    ("listen", "silent"),          # 异序词
    ("hello", "world"),            # 非异序词
    ("algorithm", "logarithm"),    # 异序词
    ("anagram", "nagaram"),        # 异序词
    ("elephant", "taphneel"),      # 非异序词
    ("restful", "flusters"),       # 异序词
]

print("\n测试结果:")
print("-" * 60)
for s1, s2 in test_cases:
    result = anagramSolution4(s1.lower().replace(" ", "a"))  # 确保只有字母
    # 简化测试
    result = anagramSolution4_v2(s1.replace(" ", "").lower(), s2.replace(" ", "").lower())
    status = "是异序词 ✓" if result else "不是异序词 ✗"
    print(f"'{s1}' vs '{s2}': {status}")
print("-" * 60)

print("\n时间复杂度分析:")
print("=" * 60)
print("方案4 计数比较法:")
print("  1. 创建两个计数器列表: O(1)")
print("  2. 遍历第一个字符串统计: O(n)")
print("  3. 遍历第二个字符串统计: O(n)")
print("  4. 比较两个计数器列表: O(1) - 固定26个位置")
print("  总时间复杂度: O(n)")
print("\n空间复杂度: O(1) - 固定大小的计数器列表（26个位置）")
print("=" * 60)
