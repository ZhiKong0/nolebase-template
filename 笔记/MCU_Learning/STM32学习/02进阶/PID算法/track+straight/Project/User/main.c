#include "stm32f10x.h"
#include "Delay.h"
#include "Control.h"

int main(void)
{
    Control_Init();

    while (1)
    {
        Control_Tick();
        Delay_ms(1);
    }
}
