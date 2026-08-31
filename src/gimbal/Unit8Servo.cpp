#include "Unit8Servo.h"

Unit8Servo::Unit8Servo(uint8_t addr) : _addr(addr), _wire(&Wire), _sda(2), _scl(1) {}

bool Unit8Servo::begin(TwoWire *wire, uint8_t sda, uint8_t scl, uint32_t freq) {
    _wire = wire;
    _sda = sda;
    _scl = scl;
    _wire->begin(_sda, _scl, freq);
    delay(20);
    return isConnected();
}

bool Unit8Servo::isConnected() {
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
    uint8_t data[8];
    memset(data, (uint8_t)mode, 8);
    return writeBytes(UNIT_8SERVO_MODE_REG, data, 8);
}

bool Unit8Servo::setPinMode(uint8_t pin, servo_pin_mode_t mode) {
    if (pin > 7) return false;
    uint8_t val = (uint8_t)mode;
    return writeBytes(UNIT_8SERVO_MODE_REG + pin, &val, 1);
}

bool Unit8Servo::setServoAngle(uint8_t pin, uint8_t angle) {
    if (pin > 7) return false;
    if (angle > 180) angle = 180;
    return writeBytes(UNIT_8SERVO_SERVO_ANGLE_8B_REG + pin, &angle, 1);
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
