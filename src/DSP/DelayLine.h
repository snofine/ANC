#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace anc::dsp {

class DelayLine {
public:
    explicit DelayLine(std::size_t capacity = 4800) : buffer_(capacity + 2, 0.0f) {}

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    void setDelay(float samples) noexcept {
        delay_ = std::clamp(samples, 0.0f, static_cast<float>(buffer_.size() - 2));
    }

    float process(float x) noexcept {
        buffer_[write_] = x;
        float read = static_cast<float>(write_) - delay_;
        while (read < 0.0f) read += static_cast<float>(buffer_.size());
        const auto i0 = static_cast<std::size_t>(read) % buffer_.size();
        const auto i1 = (i0 + 1) % buffer_.size();
        const float fraction = read - std::floor(read);
        const float y = buffer_[i0] + fraction * (buffer_[i1] - buffer_[i0]);
        write_ = (write_ + 1) % buffer_.size();
        return y;
    }

private:
    std::vector<float> buffer_;
    std::size_t write_{};
    float delay_{};
};

} // namespace anc::dsp
