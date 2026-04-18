from math import gcd

class Fraction:
    def __init__(self, top, bottom):
        if bottom < 0:
            top, bottom = -top, -bottom
        common = gcd(abs(top), abs(bottom))
        self.num, self.den = top // common, bottom // common
    
    def __str__(self):
        return f"{self.num}/{self.den}"
    
    def __repr__(self):
        return f"Fraction({self.num}, {self.den})"
    
    def show(self):
        print(f"{self.num}/{self.den}")
    
    def getnum(self):
        return self.num
    
    def getden(self):
        return self.den
    
    def __add__(self, other):
        new_num = self.num * other.den + other.num * self.den
        new_den = self.den * other.den
        return Fraction(new_num, new_den)
    
    def __sub__(self, other):
        new_num = self.num * other.den - other.num * self.den
        new_den = self.den * other.den
        return Fraction(new_num, new_den)
    
    def __mul__(self, other):
        new_num = self.num * other.num
        new_den = self.den * other.den
        return Fraction(new_num, new_den)
    
    def __truediv__(self, other):
        new_num = self.num * other.den
        new_den = self.den * other.num
        return Fraction(new_num, new_den)
    
    def __eq__(self, other):
        return self.num == other.num and self.den == other.den
    
    def __lt__(self, other):
        return self.num * other.den < other.num * self.den
    
    def __gt__(self, other):
        return self.num * other.den > other.num * self.den


def main():
 
    print("任务一：Fraction分数类的测试")
    
    print("\n1. 创建分数对象")
    X = Fraction(1, 2)
    Y = Fraction(1, 4)
    print(f"X = 1/2 创建成功: {X}")
    print(f"Y = 1/4 创建成功: {Y}")
    
    print("\n2. 使用show()方法显示分数")
    print("X.show() = ", end="")
    X.show()
    print("Y.show() = ", end="")
    Y.show()
    
    print("\n3. 使用getnum()和getden()方法")
    print(f"X.getnum() = {X.getnum()}")
    print(f"X.getden() = {X.getden()}")
    print(f"Y.getnum() = {Y.getnum()}")
    print(f"Y.getden() = {Y.getden()}")
    
    print("\n4. 加法运算")
    Z = X + Y
    print(f"X + Y = {X} + {Y} = {Z}")
    
    print("\n5. 减法运算")
    Z = X - Y
    print(f"X - Y = {X} - {Y} = {Z}")
    
    print("\n6. 乘法运算")
    Z = X * Y
    print(f"X * Y = {X} * {Y} = {Z}")
    
    print("\n7. 除法运算")
    Z = X / Y
    print(f"X / Y = {X} / {Y} = {Z}")
    
    print("\n8. 比较运算")
    A = Fraction(1, 2)
    B = Fraction(2, 4)
    print(f"A = 1/2, B = 2/4")
    print(f"A == B: {A == B}")
    print(f"X > Y: {X > Y}")
    print(f"X < Y: {X < Y}")
    





if __name__ == "__main__":
    main()
