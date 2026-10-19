#include "spring_animator.h"
#include <algorithm>

const SpringConfig SpringConfig::Default(0.29f, 0.75f, false);
const SpringConfig SpringConfig::Smooth(0.39f, 0.85f, false);
const SpringConfig SpringConfig::Snappy(0.31f, 0.80f, false);
const SpringConfig SpringConfig::Bouncy(0.25f, 0.45f, false);
const SpringConfig SpringConfig::Interactive(0.14f, 1.00f, false);

static constexpr float TWO_PI_F = 6.2831855f;
static constexpr float E_F = 2.7182817f;
static constexpr float SUBSTEP = 1.0f / 240.0f; // 240Hz 高频子步确保物理稳定

// ESP32-S3 单精度快速 Padé (2,2) 指数衰减近似, 避免 double 软浮点
static inline float expNegFast(float x) {
    if (x <= 0.0f) return 1.0f;
    if (x > 16.0f) return 0.0f;
    int n = 0;
    while (x > 0.5f) {
        x *= 0.5f;
        n++;
    }
    float x2 = x * x * (1.0f / 12.0f);
    float h = x * 0.5f;
    float r = (1.0f - h + x2) / (1.0f + h + x2);
    while (n > 0) {
        r *= r;
        n--;
    }
    return r;
}

std::vector<SpringValue*> SpringScheduler::s_activeValues;
float SpringScheduler::s_timeScale = 1.0f;

SpringValue::SpringValue(float initial, const SpringConfig& config)
    : _current(initial),
      _target(initial),
      _velocity(0.0f),
      _precision(0.05f),
      _config(config),
      _parent(nullptr),
      _active(false) {
}

SpringValue::~SpringValue() {
    SpringScheduler::unregisterActive(this);
}

float SpringValue::get() const {
    if (_parent != nullptr) {
        return _current + _parent->get();
    }
    return _current;
}

void SpringValue::wake() {
    if (!_active) {
        _active = true;
        SpringScheduler::registerActive(this);
    }
}

void SpringValue::set(float target) {
    if (target == _target && _active) {
        return;
    }
    if (target == _target && !_active && std::abs(_current - target) < _precision) {
        return;
    }
    _target = target;
    wake();
}

void SpringValue::jump(float value) {
    _current = value;
    _target = value;
    _velocity = 0.0f;
    if (_active) {
        _active = false;
        SpringScheduler::unregisterActive(this);
    }
}

void SpringValue::shift(float delta) {
    if (delta == 0.0f) return;
    _current += delta;
    wake();
}

void SpringValue::kick(float velocity) {
    _velocity += velocity;
    wake();
}

void SpringValue::bump(float px) {
    float rate = (_config.response > 0.001f) ? (TWO_PI_F / _config.response) : 20.0f;
    kick(px * rate * E_F);
}

float SpringValue::rubberBand(float x, float dim, float coeff) {
    if (dim <= 0.0f) return 0.0f;
    bool neg = x < 0.0f;
    if (neg) x = -x;
    float y = (1.0f - 1.0f / (x * coeff / dim + 1.0f)) * dim;
    return neg ? -y : y;
}

void SpringValue::step(float dt) {
    float rate = (_config.response > 0.001f) ? (TWO_PI_F / _config.response) : 20.0f;
    float x = _current - _target;
    float vel = _velocity;

    if (_config.ease) {
        x = x * expNegFast(rate * dt);
        vel = 0.0f;
    } else {
        float k = rate * rate;
        float c = 2.0f * _config.damping * rate;
        int n = static_cast<int>(dt / SUBSTEP) + 1;
        float h = dt / static_cast<float>(n);

        for (int i = 0; i < n; i++) {
            vel += (-k * x - c * vel) * h;
            x += vel * h;
        }
    }

    float p = (_precision > 0.0f) ? _precision : 0.05f;
    if (std::abs(x) < p && std::abs(vel) < p * 10.0f) {
        _current = _target;
        _velocity = 0.0f;
        _active = false;
        return;
    }

    _current = _target + x;
    _velocity = vel;
}

bool SpringScheduler::update(float dt) {
    if (s_activeValues.empty()) {
        return false;
    }
    if (dt <= 0.0f) {
        return true;
    }
    if (dt > 0.1f) {
        dt = 0.1f; // 限制单帧最大时间增量, 防止系统唤醒/阻塞带来的时间跳跃
    }
    dt *= s_timeScale;

    for (size_t i = 0; i < s_activeValues.size();) {
        SpringValue* val = s_activeValues[i];
        val->step(dt);
        if (val->_active) {
            i++;
        } else {
            // O(1) 交换删除
            size_t last = s_activeValues.size() - 1;
            s_activeValues[i] = s_activeValues[last];
            s_activeValues.pop_back();
        }
    }
    return !s_activeValues.empty();
}

bool SpringScheduler::isBusy() {
    return !s_activeValues.empty();
}

void SpringScheduler::registerActive(SpringValue* val) {
    if (std::find(s_activeValues.begin(), s_activeValues.end(), val) == s_activeValues.end()) {
        s_activeValues.push_back(val);
    }
}

void SpringScheduler::unregisterActive(SpringValue* val) {
    auto it = std::find(s_activeValues.begin(), s_activeValues.end(), val);
    if (it != s_activeValues.end()) {
        // 与最后一个元素交换以 O(1) 移除
        *it = s_activeValues.back();
        s_activeValues.pop_back();
    }
    val->_active = false;
}
