"""
任务一：Python列表的内置应用操作
练习列表的常用方法：append, insert, pop, del等
"""

# 创建列表
A = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
print("=" * 50)
print("任务一：Python列表内置操作练习")
print("=" * 50)

print(f"\n1. 初始列表 A = {A}")

# 1. append - 在列表末尾添加新元素
print("\n--- append 操作 ---")
A.append(10)  # 添加元素10
print(f"A.append(10) 后: A = {A}")
print("解释: append() 在列表末尾添加一个新元素")

# 2. insert - 在指定位置插入元素
print("\n--- insert 操作 ---")
A.insert(0, -1)  # 在索引0位置插入-1
print(f"A.insert(0, -1) 后: A = {A}")
print("解释: insert(i, item) 在列表的第i个位置插入一个元素")

# 3. pop - 删除并返回列表中最后一个元素
print("\n--- pop 操作（无参数） ---")
popped_item = A.pop()
print(f"A.pop() 返回: {popped_item}, 列表变为: A = {A}")
print("解释: pop() 删除并返回列表中最后一个元素")

# 4. pop(i) - 删除并返回列表中第i个位置的元素
print("\n--- pop(i) 操作 ---")
popped_item_i = A.pop(0)  # 删除索引0位置的元素
print(f"A.pop(0) 返回: {popped_item_i}, 列表变为: A = {A}")
print("解释: pop(i) 删除并返回列表中第i个位置的元素")

# 5. sort - 将列表元素排序
print("\n--- sort 操作 ---")
B = [3, 1, 4, 1, 5, 9, 2, 6]
print(f"排序前: B = {B}")
B.sort()
print(f"B.sort() 排序后: B = {B}")
print("解释: sort() 将列表元素按升序排序")

# 6. reverse - 将列表元素倒序排列
print("\n--- reverse 操作 ---")
B.reverse()
print(f"B.reverse() 倒序后: B = {B}")
print("解释: reverse() 将列表元素倒序排列")

# 7. del - 删除列表中第i个位置的元素
print("\n--- del 操作 ---")
C = [10, 20, 30, 40, 50]
print(f"操作前: C = {C}")
del C[2]  # 删除索引2位置的元素
print(f"del C[2] 后: C = {C}")
print("解释: del alist[i] 删除列表中第i个位置的元素")

# 8. index - 返回item第一次出现时的下标
print("\n--- index 操作 ---")
D = ['a', 'b', 'c', 'd', 'b']
print(f"D = {D}")
idx = D.index('b')
print(f"D.index('b') 返回: {idx}")
print("解释: index(item) 返回item第一次出现时的下标")

# 9. count - 返回item在列表中出现的次数
print("\n--- count 操作 ---")
print(f"D = {D}")
cnt = D.count('b')
print(f"D.count('b') 返回: {cnt}")
print("解释: count(item) 返回item在列表中出现的次数")

# 10. remove - 从列表中移除第一次出现的item
print("\n--- remove 操作 ---")
print(f"操作前: D = {D}")
D.remove('b')  # 移除第一个'b'
print(f"D.remove('b') 后: D = {D}")
print("解释: remove(item) 从列表中移除第一次出现的item")

print("\n" + "=" * 50)
print("所有列表内置操作练习完成！")
print("=" * 50)
