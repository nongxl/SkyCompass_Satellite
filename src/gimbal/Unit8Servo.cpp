#include "Unit8Servo.h"

Unit8Servo::Unit8Servo(uint8_t addr) : _addr(addr), _wire(&Wire), _sda(2), _scl(1), _driverType(DRIVER_TYPE_NONE) {}

bool Unit8Servo::initPCA9685() {
    // PCA9685 软件复位与唤醒
    uint8_t resetVal = 0x80;
    writeBytes(PCA9685_MODE1_REG, &resetVal, 1);
    delay(10);

    // 设置 50Hz 刷新频率 (周期 20ms)
    // 预分频公式: round(25000000.0 / (4096.0 * 50.0)) - 1 = 121 (0x79)
    uint8_t sleepMode = 0x10; // 置 SLEEP 位
    writeBytes(PCA9685_MODE1_REG, &sleepMode, 1);
    delay(5);

    uint8_t prescale = 121;
    writeBytes(PCA9685_PRESCALE_REG, &prescale, 1);
    delay(5);

    // 唤醒并开启自动递增 (Auto-Increment)
    uint8_t normalMode = 0xA0; // Restart (bit 7) + Auto-Increment (bit 5)
    writeBytes(PCA9685_MODE1_REG, &normalMode, 1);
    delay(5);
    
    return true;
}

bool Unit8Servo::begin(TwoWire *wire, uint8_t sda, uint8_t scl, uint32_t freq) {
    _wire = wire;
    _sda = sda;
    _scl = scl;
    _wire->begin(_sda, _scl, freq);
    delay(20);

    // 自动扫描识别驱动板类型
    // 1. 优先探测 PCA9685 (M5Stack Module SERVO2 默认 0x40)
    _wire->beginTransmission(PCA9685_DEFAULT_ADDR);
    if (_wire->endTransmission() == 0) {
        _addr = PCA9685_DEFAULT_ADDR;
        _driverType = DRIVER_TYPE_PCA9685;
        initPCA9685();
        log_i("[Gimbal] Auto-detected M5Stack Module SERVO2 (PCA9685) at 0x%02X", _addr);
        return true;
    }

    // 2. 备用探测 Unit 8Servos (STM32 方案默认 0x25)
    _wire->beginTransmission(UNIT_8SERVO_DEFAULT_ADDR);
    if (_wire->endTransmission() == 0) {
        _addr = UNIT_8SERVO_DEFAULT_ADDR;
        _driverType = DRIVER_TYPE_UNIT8SERVO;
        log_i("[Gimbal] Auto-detected M5Stack Unit 8Servos at 0x%02X", _addr);
        return true;
    }

    // 3. 全总线扫描以便输出调试诊断日志
    log_i("[Gimbal] Scanning Grove I2C bus (GPIO 2/1)...");
    uint8_t foundCount = 0;
    for (uint8_t testAddr = 1; testAddr < 127; testAddr++) {
        _wire->beginTransmission(testAddr);
        if (_wire->endTransmission() == 0) {
            log_i("[Gimbal] -> Found responding I2C device at 0x%02X", testAddr);
            foundCount++;
            if (_driverType == DRIVER_TYPE_NONE) {
                if (testAddr >= 0x40 && testAddr <= 0x47) {
                    _addr = testAddr;
                    _driverType = DRIVER_TYPE_PCA9685;
                    initPCA9685();
                } else if (testAddr == UNIT_8SERVO_DEFAULT_ADDR) {
                    _addr = testAddr;
                    _driverType = DRIVER_TYPE_UNIT8SERVO;
                }
            }
        }
    }
    if (foundCount == 0) {
        log_i("[Gimbal] No I2C devices found on Grove port. Please check wiring (VCC/GND/SDA/SCL)!");
    }

    return (_driverType != DRIVER_TYPE_NONE);
}

bool Unit8Servo::isConnected() {
    if (_driverType == DRIVER_TYPE_NONE) return false;
    _wire->beginTransmission(_addr);
    return (_wire->endTransmission() == 0);
}

bool Unit8Servo::writeBytes(uint8_t reg, const uint8_t *buffer, uint8_t length) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    for (uint8_t i = 0; i < length; i++) {
        _wire->write(buffer[i]);
    }
    return (_wire->endTransmission() == 0);
}

bool Unit8Servo::readBytes(uint8_t reg, uint8_t *buffer, uint8_t length) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) {
        return false;
    }
    if (_wire->requestFrom(_addr, length) == length) {
        for (uint8_t i = 0; i < length; i++) {
            buffer[i] = _wire->read();
        }
        return true;
    }
    return false;
}

bool Unit8Servo::setAllPinMode(servo_pin_mode_t mode) {
    if (_driverType == DRIVER_TYPE_PCA9685) {
        return true; // PCA9685 默认为 PWM/舵机驱动
    }
    uint8_t data[8];
    memset(data, (uint8_t)mode, 8);
    return writeBytes(UNIT_8SERVO_MODE_REG, data, 8);
}

bool Unit8Servo::setPinMode(uint8_t pin, servo_pin_mode_t mode) {
    if (_driverType == DRIVER_TYPE_PCA9685) return true;
    if (pin > 7) return false;
    uint8_t val = (uint8_t)mode;
    return writeBytes(UNIT_8SERVO_MODE_REG + pin, &val, 1);
}

bool Unit8Servo::setServoAngle(uint8_t pin, uint8_t angle) {
    if (angle > 180) angle = 180;

    if (_driverType == DRIVER_TYPE_PCA9685) {
        if (pin > 15) return false;
        // 50Hz 周期 20ms (20000us) 对应 4096 计数
        // 0° -> 500us (约 102 计数), 180° -> 2500us (约 512 计数)
        uint16_t offCount = 102 + ((uint32_t)angle * (512 - 102)) / 180;
        if (offCount > 4095) offCount = 4095;

        uint8_t reg = PCA9685_LED0_ON_L_REG + 4 * pin;
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->write(0x00); // ON_L
        _wire->write(0x00); // ON_H
        _wire->write((uint8_t)(offCount & 0xFF));        // OFF_L
        _wire->write((uint8_t)((offCount >> 8) & 0x0F)); // OFF_H
        return (_wire->endTransmission() == 0);
    } else {
        if (pin > 7) return false;
        return writeBytes(UNIT_8SERVO_SERVO_ANGLE_8B_REG + pin, &angle, 1);
    }
}

bool Unit8Servo::setServoPulse(uint8_t pin, uint16_t pulse) {
    if (pin > 7) return false;
    uint8_t data[2];
    data[0] = pulse & 0xFF;         // 低位
    data[1] = (pulse >> 8) & 0xFF;  // 高位
    return writeBytes(UNIT_8SERVO_SERVO_PULSE_16B_REG + pin * 2, data, 2);
}

bool Unit8Servo::setPWM(uint8_t pin, uint8_t pwm) {
    if (pin > 7) return false;
    return writeBytes(UNIT_8SERVO_PWM_8B_REG + pin, &pwm, 1);
}

bool Unit8Servo::setLEDColor(uint8_t pin, uint32_t color) {
    if (pin > 7) return false;
    uint8_t data[3];
    data[0] = (color >> 16) & 0xFF; // R
    data[1] = (color >> 8) & 0xFF;  // G
    data[2] = color & 0xFF;         // B
    return writeBytes(UNIT_8SERVO_RGB_24B_REG + pin * 3, data, 3);
}

bool Unit8Servo::setDigitalOutput(uint8_t pin, uint8_t state) {
    if (pin > 7) return false;
    uint8_t val = state ? 1 : 0;
    return writeBytes(UNIT_8SERVO_OUTPUT_CTL_REG + pin, &val, 1);
}

uint8_t Unit8Servo::getDigitalInput(uint8_t pin) {
    if (pin > 7) return 0;
    uint8_t val = 0;
    readBytes(UNIT_8SERVO_DIGITAL_INPUT_REG + pin, &val, 1);
    return val;
}

uint16_t Unit8Servo::getAnalogInput(uint8_t pin, analog_read_mode_t bit) {
    if (pin > 7) return 0;
    if (bit == ANALOG_8BIT) {
        uint8_t val = 0;
        readBytes(UNIT_8SERVO_ANALOG_INPUT_8B_REG + pin, &val, 1);
        return val;
    } else {
        uint8_t data[2] = {0};
        if (readBytes(UNIT_8SERVO_ANALOG_INPUT_12B_REG + pin * 2, data, 2)) {
            return ((uint16_t)data[1] << 8) | data[0];
        }
    }
    return 0;
}

float Unit8Servo::getCurrent() {
    uint8_t data[4] = {0};
    if (readBytes(UNIT_8SERVO_CURRENT_REG, data, 4)) {
        float current = 0.0f;
        memcpy(&current, data, 4);
        return current;
    }
    return 0.0f;
}

uint8_t Unit8Servo::getFirmwareVersion() {
    uint8_t ver = 0;
    readBytes(UNIT_8SERVO_FW_VERSION_REG, &ver, 1);
    return ver;
}

bool Unit8Servo::setI2CAddress(uint8_t new_addr) {
    if (writeBytes(UNIT_8SERVO_I2C_ADDR_REG, &new_addr, 1)) {
        _addr = new_addr;
        return true;
    }
    return false;
}
