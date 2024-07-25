#include <Arduino.h>
#include "Wire.h"
#include "SPI.h"
#include "stm32l0xx.h"
#include "stm32l0xx_hal.h"

#include <HardwareTimer.h>
#include <STM32FreeRTOS.h>
#include <STM32LowPower.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_LIS2DW12.h>
#include <DFRobot_DS323X.h>
// #include <RTClib.h>

#include "evlk_74hc595.h"
using namespace _EVLK_74HC595_;
#include "evlk_hpdl1414.h"
using namespace _EVLK_HPDL1414_;

#define PIN_KEY1 PA12
#define PIN_KEY2 PA11
#define PIN_ADC PA3
#define PIN_INT1 PA0
#define PIN_INT2 PA1
#define PIN_INT3 PA2
#define PIN_LED PA8
#define PIN_RGB PB3
#define PIN_SDA PA7
#define PIN_SCL PA6
#define PIN_EN_LIGHT PA15
#define PIN_DS PB2
#define PIN_ST PB1
#define PIN_SH PB0
#define PIN_D2_A0 PA5
#define PIN_D2_A1 PA4
#define PIN_D3_A0 PA7
#define PIN_D3_A1 PA6
#define RGB_COUNT 1

#define BUILD_DEBUG_MODE 1
#define BUILD_RELEASE_MODE 0
#define BUILD_MODE BUILD_DEBUG_MODE

enum PWR_LEVEL
{
    PWR_LOW,
    PWR_MID,
    PWR_HIGH,
    PWR_FULL,
};
PWR_LEVEL sysPwrLevel = PWR_FULL;

enum HCLK_FREQ
{
    HCLK_FREQ_32M,
    HCLK_FREQ_2M,
};

// #define delay_t(ms) delay(ms)
#define delay_t(ms) vTaskDelay(pdMS_TO_TICKS(ms));

void SystemClock_ConfigS(HCLK_FREQ);
void SystemSoftReset(void);

hc595 HC595(PIN_DS, PIN_ST, PIN_SH, 2);
hpdl1414 GLED1(HC595[1][7], HC595[1][6], HC595[1][5], HC595[1][4], HC595[1][1], HC595[1][2], HC595[1][3], PIN_D3_A0, PIN_D3_A1, HC595[0][0]);
hpdl1414 GLED2(HC595[0][7], HC595[0][6], HC595[0][5], HC595[0][4], HC595[0][1], HC595[0][2], HC595[0][3], PIN_D2_A0, PIN_D2_A1, HC595[0][0]);
DFRobot_LIS2DW12_I2C acce;
DFRobot_DS323X rtc;
Adafruit_NeoPixel rgb(RGB_COUNT, PIN_RGB, NEO_RGB + NEO_KHZ800);

void SystemPowerDown()
{
    HC595.~hc595();
    rgb.~Adafruit_NeoPixel();
    pinMode(PIN_EN_LIGHT, INPUT_FLOATING);
    // while (1)
    //     ;

    LowPower.deepSleep();
    SystemSoftReset();
}
double magicPwrFunction(double vot)
{
    if (vot <= 2.5)
        return 0;
    if (vot <= 3.68f)
        return 5 * vot - 12.5;
    return -263.2 * vot * vot + 2249.2 * vot - 4705.2;
}
double adc(uint32_t adcValue)
{
    return (adcValue / 4095.0) * 2 * 3.3;
}
PWR_LEVEL getPwrLevel(uint32_t pin)
{
    analogReadResolution(12);
    pinMode(pin, INPUT_ANALOG);
    double per = magicPwrFunction(adc(analogRead(pin)));
    if (per >= 95)
        return PWR_FULL;
    if (per >= 70)
        return PWR_HIGH;
    if (per >= 30)
        return PWR_MID;
    return PWR_LOW;
};

#define pwmPeriod 20
static int __pwmp_ch1;
static int __pwmv_ch1 = 0;

void myPWMPeried_CH1(void *)
{
    static int value = 0;
    for (;;)
    {
        if (value != __pwmv_ch1)
            value = __pwmv_ch1;
        else
        {
            int HT = (constrain(__pwmv_ch1, 0, 255) / 255.0) * pwmPeriod;
            digitalWrite(__pwmp_ch1, HIGH);
            delay_t(HT);
            digitalWrite(__pwmp_ch1, LOW);
            delay_t(pwmPeriod - HT);
        }
    }
}
void myAnalogWrite(int ch, int pin, int value)
{
    switch (ch)
    {
    case 1:
        __pwmp_ch1 = pin;
        __pwmv_ch1 = value;
        break;
    default:
        break;
    }
};

void hpdl1414_print(const char *str, size_t len)
{
    len = len > 8 ? 8 : len;
    char Buffer[4] = {0};
    memcpy(Buffer, str, len);
    GLED1.write(Buffer, 4);
    memset(Buffer, 0, 4);
    if (len <= 4)
        GLED2.write(Buffer, 4);
    else
    {
        memcpy(Buffer, str + 4, len - 4);
        GLED2.write(Buffer, 4);
    }
};
void hpdl1414_print(const char *str) { hpdl1414_print(str, strlen(str)); };

void rtc_init()
{
    rtc.disableAlarm1Int();
    rtc.disableAlarm2Int();
    rtc.writeSqwPinMode(rtc.eSquareWave_OFF);
    rtc.setTime(1970, 1, 1, 0, 0, 0);
    // rtc.writeSqwPinMode(DS3231_OFF);
    // rtc.disable32K();
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}
void acce_init()
{
    acce.softReset();
    acce.setRange(DFRobot_LIS2DW12::e2_g);
    acce.setPowerMode(DFRobot_LIS2DW12::eContLowPwrLowNoise1_12bit);
    acce.setDataRate(DFRobot_LIS2DW12::eRate_12hz5);

    acce.enableTapDetectionOnZ(true);
    acce.enableTapDetectionOnY(false);
    acce.enableTapDetectionOnX(false);
    // acce.setTapThresholdOnX(1);
    // acce.setTapThresholdOnY(1);
    acce.setTapThresholdOnZ(0.8);

    // acce.setFilterPath(DFRobot_LIS2DW12::eLPF);
    acce.setTapDur(1);
    acce.setTapMode(DFRobot_LIS2DW12::eBothSingleDouble);
    acce.setInt1Event(DFRobot_LIS2DW12::eDoubleTap);
}
void acce_init2()
{
    acce.softReset();
    acce.setRange(DFRobot_LIS2DW12::e2_g);
    acce.setPowerMode(DFRobot_LIS2DW12::eContLowPwrLowNoise1_12bit);
    acce.setDataRate(DFRobot_LIS2DW12::eRate_50hz);

    // acce.setFilterPath(DFRobot_LIS2DW12::eLPF);
    acce.set6DThreshold(DFRobot_LIS2DW12::eDegrees60);
    acce.setTapDur(1);
    acce.setInt1Event(DFRobot_LIS2DW12::e6D);
}

void isLostPower()
{
    rtc_init();
    if (!acce.begin())
        Serial.println("Couldn't find ACCE");
    acce_init();
}
#define ledBreathInterval1 5
#define ledBreathInterval2 200
#define ledMaxBright 150
void ledBreath(void *)
{
    for (;;)
    {
        for (int x = 0; x < 180; x++)
        {
            float sinval = (sin(x * (PI / 180)));
            myAnalogWrite(1, PIN_LED, sinval * ledMaxBright);
            delay_t(ledBreathInterval1);
        }
        delay_t(ledBreathInterval2);
    }
};
void rgbConst(int pin, uint32_t color)
{
    rgb.setPixelColor(0, color);
    rgb.show();
};
#define printTimeTimes 4
#define printDateTimes 2
#define printTimeInterval 1000
#define printDateInterval 1000
void printTime(void *)
{
    char strTime[9];
    for (int i = 0; i < printTimeTimes; i++)
    {
        strcpy(strTime, "00:00:00");
        uint8_t dates[3] = {rtc.getHour(), rtc.getMinute(), rtc.getSecond()};
        for (size_t i = 0; i < 3; i++)
        {
            strTime[i * 3] = dates[i] / 10 + '0';
            strTime[i * 3 + 1] = dates[i] % 10 + '0';
        }
        hpdl1414_print(strTime);
        delay_t(printTimeInterval);
    }
    if (sysPwrLevel > PWR_LOW)
        for (int i = 0; i < printDateTimes; i++)
        {
            strcpy(strTime, "00-00-00");
            uint8_t dates[3] = {rtc.getYear() % 100, rtc.getMonth(), rtc.getDate()};
            for (size_t i = 0; i < 3; i++)
            {
                strTime[i * 3] = dates[i] / 10 + '0';
                strTime[i * 3 + 1] = dates[i] % 10 + '0';
            }
            hpdl1414_print(strTime);
            delay_t(printDateInterval);
        }
    SystemPowerDown();
};

void setup()
{
    HAL_Init();
    sysPwrLevel = getPwrLevel(PIN_ADC);
    if (sysPwrLevel == PWR_LOW)
        SystemClock_ConfigS(HCLK_FREQ_2M);
    else
        SystemClock_ConfigS(HCLK_FREQ_32M);

    // pinMode(PIN_KEY1, INPUT);
    // pinMode(PIN_KEY2, INPUT);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_EN_LIGHT, OUTPUT_OPENDRAIN);
    digitalWrite(PIN_EN_LIGHT, HIGH);
    digitalWrite(PIN_EN_LIGHT, LOW);

    if (sysPwrLevel == PWR_LOW)
    {
        digitalWrite(PIN_LED, HIGH);
        delay(200);
        digitalWrite(PIN_LED, LOW);
    }

    Wire.begin();
    if (!rtc.begin())
        Serial.println("Couldn't find RTC");
    if (rtc.isLostPower()) // 掉电检测
        isLostPower();

    if (sysPwrLevel > PWR_LOW)
        rgb.begin();
    HC595.Begin();
    GLED1.Begin();
    GLED2.Begin();

    xTaskCreate(printTime, "printTime", 64, NULL, 3, NULL);

    if (sysPwrLevel > PWR_LOW)
    {
        xTaskCreate(myPWMPeried_CH1, "myAnalog", 8, NULL, 1, NULL);
        xTaskCreate(ledBreath, "ledBreath", 8, NULL, 1, NULL);
        if (sysPwrLevel == PWR_MID)
            rgbConst(PIN_RGB, Adafruit_NeoPixel::Color(200, 0, 0)); // RED
        if (sysPwrLevel >= PWR_HIGH)
            rgbConst(PIN_RGB, Adafruit_NeoPixel::Color(0, 50, 255)); // BLUE
    }

    // attachInterrupt(digitalPinToInterrupt(PIN_INT1), SystemSoftReset, CHANGE);
    LowPower.begin();
    LowPower.attachInterruptWakeup(PIN_INT1, SystemSoftReset, CHANGE, DEEP_SLEEP_MODE);
    vTaskStartScheduler();
};
void loop() {};

void SystemClock_Config(void) { SystemClock_ConfigS(HCLK_FREQ_32M); }
void SystemClock_ConfigS(HCLK_FREQ freq)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    switch (freq)
    {
    case HCLK_FREQ_32M:
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        RCC_OscInitStruct.HSIState = RCC_HSI_ON;
        RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
        RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
        RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_4;
        RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_2;
        break;
    case HCLK_FREQ_2M:
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
        RCC_OscInitStruct.MSIState = RCC_MSI_ON;
        RCC_OscInitStruct.MSICalibrationValue = 0;
        RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_5;
        RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
        break;
    default:
        break;
    }

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    switch (freq)
    {
    case HCLK_FREQ_32M:
        RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
        break;
    case HCLK_FREQ_2M:
        RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
        break;
    default:
        break;
    }

    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    switch (freq)
    {
    case HCLK_FREQ_32M:
        if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
            Error_Handler();
        break;
    case HCLK_FREQ_2M:
        if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
            Error_Handler();
        break;
    }
}
void SystemSoftReset(void)
{
    __set_PRIMASK(1);   // 禁止所有的可屏蔽中断
    NVIC_SystemReset(); // 软件复位
};

int main()
{
    setup();
    while (1)
        loop();
}