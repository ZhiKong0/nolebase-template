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