class LogicGate:
    def __init__(self, n):
        self.name = n
        self.output = None
    
    def getName(self):
        return self.name
    
    def getOutput(self):
        self.output = self.performGateLogic()
        return self.output


class BinaryGate(LogicGate):
    def __init__(self, n):
        LogicGate.__init__(self, n)
        self.pinA = None
        self.pinB = None
    
    def getPinA(self):
        if self.pinA is None:
            return int(input(f"请输入 {self.getName()} 的引脚A: "))
        else:
            return self.pinA.getFrom().getOutput()
    
    def getPinB(self):
        if self.pinB is None:
            return int(input(f"请输入 {self.getName()} 的引脚B: "))
        else:
            return self.pinB.getFrom().getOutput()
    
    def setNextPin(self, source):
        if self.pinA is None:
            self.pinA = source
        else:
            if self.pinB is None:
                self.pinB = source
            else:
                raise RuntimeError("错误: 没有可用的引脚!")


class UnaryGate(LogicGate):
    def __init__(self, n):
        LogicGate.__init__(self, n)
        self.pin = None
    
    def getPin(self):
        if self.pin is None:
            return int(input(f"请输入 {self.getName()} 的引脚: "))
        else:
            return self.pin.getFrom().getOutput()
    
    def setNextPin(self, source):
        if self.pin is None:
            self.pin = source
        else:
            raise RuntimeError("错误: 没有可用的引脚!")


class AndGate(BinaryGate):
    def performGateLogic(self):
        a = self.getPinA()
        b = self.getPinB()
        if a == 1 and b == 1:
            return 1
        else:
            return 0


class OrGate(BinaryGate):
    def performGateLogic(self):
        a = self.getPinA()
        b = self.getPinB()
        if a == 1 or b == 1:
            return 1
        else:
            return 0


class NotGate(UnaryGate):
    def performGateLogic(self):
        pin = self.getPin()
        if pin == 1:
            return 0
        else:
            return 1


class Connector:
    def __init__(self, frm, to):
        self.fromgate = frm
        self.togate = to
        to.setNextPin(self)
    
    def getFrom(self):
        return self.fromgate
    
    def getTo(self):
        return self.togate


def main():
    print("任务二：逻辑门电路的继承实现测试")
    
    print("\n测试1：与门 (AndGate)")
    g1 = AndGate("G1")
    print(f"G1(与门)输出: {g1.getOutput()}")
    
    print("\n测试2：或门 (OrGate)")
    g2 = OrGate("G2")
    print(f"G2(或门)输出: {g2.getOutput()}")
    
    print("\n测试3：非门 (NotGate)")
    g3 = NotGate("G3")
    print(f"G3(非门)输出: {g3.getOutput()}")
    
    print("\n测试4：复合电路 NOT(G4 AND G5)")
    g4 = AndGate("G4")
    g5 = NotGate("G5")
    g4_output = g4.getOutput()
    g5_output = g5.getOutput()
    print(f"G4(与门)输出: {g4_output}")
    print(f"G5(非门)输出: {g5_output}")
    
    print("\n测试5：使用Connector连接电路")
    print("电路: G6(与门) --> G7(非门)")
    g6 = AndGate("G6")
    g7 = NotGate("G7")
    c1 = Connector(g6, g7)
    g6_output = g6.getOutput()
    g7_output = g7.getOutput()
    print(f"G6(与门)输出: {g6_output}")
    print(f"G7(非门)输出: {g7_output}")
    
    print("\n测试6：复杂电路测试")
    print("电路: G8(与门) --> G9(或门) --> G10(非门)")
    g8 = AndGate("G8")
    g9 = OrGate("G9")
    g10 = NotGate("G10")
    c2 = Connector(g8, g9)
    c3 = Connector(g9, g10)
    g8_output = g8.getOutput()
    g9_output = g9.getOutput()
    g10_output = g10.getOutput()
    print(f"G8(与门)输出: {g8_output}")
    print(f"G9(或门)输出: {g9_output}")
    print(f"G10(非门)输出: {g10_output}")
    



if __name__ == "__main__":
    main()
