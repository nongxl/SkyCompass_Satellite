#ifndef _UNIT_8SERVO_H_
#define _UNIT_8SERVO_H_

#include <Arduino.h>
#include <Wire.h>

#define UNIT_8SERVO_DEFAULT_ADDR        0x25
#define PCA9685_DEFAULT_ADDR            0x40

#define UNIT_8SERVO_MODE_REG            0x00
#define UNIT_8SERVO_OUTPUT_CTL_REG      0x10
#define UNIT_8SERVO_DIGITAL_INPUT_REG   0x20
#define UNIT_8SERVO_ANALOG_INPUT_8B_REG 0x30
#define UNIT_8SERVO_ANALOG_INPUT_12B_REG 0x40
#define UNIT_8SERVO_SERVO_ANGLE_8B_REG  0x50
#define UNIT_8SERVO_SERVO_PULSE_16B_REG 0x60
#define UNIT_8SERVO_RGB_24B_REG         0x70
#define UNIT_8SERVO_PWM_8B_REG          0x90
#define UNIT_8SERVO_CURRENT_REG         0xA0
#define UNIT_8SERVO_BOOTLOADER_REG      0xFD
#define UNIT_8SERVO_FW_VERSION_REG      0xFE
#define UNIT_8SERVO_I2C_ADDR_REG        0xFF

#define PCA9685_MODE1_REG               0x00
#define PCA9685_PRESCALE_REG            0xFE
#define PCA9685_LED0_ON_L_REG           0x06

typedef enum {
    DRIVER_TYPE_NONE = 0,
    DRIVER_TYPE_UNIT8SERVO, // M5Stack Unit 8Servos (0x25)
    DRIVER_TYPE_PCA9685     // M5Stack Module SERVO2 (0x40)
} servo_driver_type_t;

typedef enum {
    DIGITAL_INPUT_MODE = 0,
    DIGITAL_OUTPUT_MODE,
    ADC_INPUT_MODE,
    SERVO_CTL_MODE,
    RGB_LED_MODE,
    PWM_MODE
} servo_pin_mode_t;

typedef enum {
    ANALOG_8BIT = 0,
    ANALOG_12BIT
} analog_read_mode_t;

class Unit8Servo {
private:
    uint8_t _addr;
    TwoWire *_wire;
    uint8_t _sda;
    uint8_t _scl;
    servo_driver_type_t _driverType;

    bool writeBytes(uint8_t reg, const uint8_t *buffer, uint8_t length);
    bool readBytes(uint8_t reg, uint8_t *buffer, uint8_t length);
    bool initPCA9685();

public:
    Unit8Servo(uint8_t addr = 0);
    
    bool begin(TwoWire *wire = &Wire, uint8_t sda = 2, uint8_t scl = 1, uint32_t freq = 100000);
    bool isConnected();
    servo_driver_type_t getDriverType() const { return _driverType; }

    bool setAllPinMode(servo_pin_mode_t mode);
    bool setPinMode(uint8_t pin, servo_pin_mode_t mode);

    bool setServoAngle(uint8_t pin, uint8_t angle);
    bool setServoPulse(uint8_t pin, uint16_t pulse);
    bool setPWM(uint8_t pin, uint8_t pwm);
    bool setLEDColor(uint8_t pin, uint32_t color);
    bool setDigitalOutput(uint8_t pin, uint8_t state);

    uint8_t getDigitalInput(uint8_t pin);
    uint16_t getAnalogInput(uint8_t pin, analog_read_mode_t bit = ANALOG_8BIT);
    float getCurrent();
    uint8_t getFirmwareVersion();
    bool setI2CAddress(uint8_t new_addr);
};

#endif

