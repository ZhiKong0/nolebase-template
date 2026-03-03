def calculate_parking_fee(hours, vehicle_type="小型车", 
                         member_level="普通", is_night=False):
    # 计算停车费用
    
    # 1. 先验证和处理时长（用新变量，不覆盖 hours）
    if hours < 0:
        actual_hours = 0
    elif hours > 24:
        actual_hours = 24
    elif hours < 1:
        actual_hours = 1           # 不足1小时按1小时计
    else:
        actual_hours = hours        # 保持原值（支持浮点数）
    
    # 2. 计算计费小时数（首小时免费）
    charge_hours = actual_hours - 1
    if charge_hours < 0:
        charge_hours = 0
    
    # 3. 车型系数
    money_per_hour = 10
    if vehicle_type == "小型车":
        money_per_hour *= 1.0
    elif vehicle_type == "中型车":
        money_per_hour *= 1.5
    elif vehicle_type == "大型车":
        money_per_hour *= 2.0
    else:
        raise ValueError("不支持的车型")
    
    # 4. 计算原价（考虑封顶100）
    original = charge_hours * money_per_hour
    if original > 100:
        original = 100
    
    # 5. 会员折扣
    if member_level == "普通":
        discount = 1.0
    elif member_level == "银卡":
        discount = 0.9
    elif member_level == "金卡":
        discount = 0.8
    elif member_level == "钻石卡":
        discount = 0.7
    else:
        raise ValueError("不支持的会员等级")
    
    fee = original * discount
    
    # 6. 夜间优惠
    if is_night:
        fee -= 5
        if fee < 0:
            fee = 0
    
    return {
        "original_fee": round(original, 2),
        "discount_rate": discount,
        "night_discount": 5 if is_night else 0,
        "final_fee": round(fee, 2)
    }


# 测试浮点数
print(calculate_parking_fee(3.5, "小型车", "普通", False))
# {'original_fee': 25.0, 'discount_rate': 1.0, 'night_discount': 0, 'final_fee': 25.0}
# (3.5小时→计费2.5小时，25元)


def weight_advice(height_cm, weight_kg, age, gender):
    # 输入验证
    if age < 10 or age > 100:
        raise ValueError("年龄输入不合法")
    if height_cm < 100 or height_cm > 250:
        raise ValueError("身高输入不合法")
    
    # 计算BMI
    bmi = weight_kg / (height_cm / 100) ** 2
    
    # 确定BMI分类（使用变量存储，不提前return）
    if age < 14:
        if bmi < 18:
            category = "正常"
        else:
            category = "偏胖"
    elif 14 <= age < 18:
        if bmi < 17.5:
            category = "偏瘦"
        elif bmi < 23:
            category = "正常"
        elif bmi < 27:
            category = "偏胖"
        else:
            category = "肥胖"
    else:  # age >= 18
        if bmi < 18.5:
            category = "偏瘦"
        elif bmi < 24:
            category = "正常"
        elif bmi < 28:
            category = "偏胖"
        else:
            category = "肥胖"
    
    # 极端BMI警告
    if bmi < 15 or bmi > 40:
        warning = "建议立即就医检查"
    else:
        warning = None
    
    # 计算BMR
    if gender == "男":
        bmr = 10 * weight_kg + 6.25 * height_cm - 5 * age + 5
    elif gender == "女":
        bmr = 10 * weight_kg + 6.25 * height_cm - 5 * age - 161
    else:
        raise ValueError("不支持的性别")
    
    # 根据分类确定建议
    if category == "偏瘦":
        daily_calories = int(bmr * 1.4)
        advice = "需要增加营养摄入，建议少食多餐"
        exercise = "以力量训练为主，每周3次，每次45分钟"
    elif category == "正常":
        daily_calories = int(bmr * 1.2)
        advice = "保持当前状态，注意饮食均衡"
        exercise = "有氧运动+力量训练结合，每周4次，每次40分钟"
    elif category == "偏胖":
        daily_calories = int(bmr * 1.1)
        advice = "控制饮食，减少高热量食物"
        exercise = "以有氧运动为主，每周5次，每次50分钟"
    else:  # 肥胖
        daily_calories = int(bmr * 1.0)
        advice = "建议咨询医生，制定严格减重计划"
        exercise = "从低强度运动开始，如快走、游泳，循序渐进"
    
    if warning:
        advice = warning + "；" + advice
    
    return {
        "bmi": round(bmi, 1),
        "category": category,
        "bmr": int(bmr),
        "daily_calories": daily_calories,
        "advice": advice,
        "exercise_plan": exercise
    }


# 测试
print(weight_advice(175, 70, 25, "男"))
print(weight_advice(160, 45, 20, "女"))
