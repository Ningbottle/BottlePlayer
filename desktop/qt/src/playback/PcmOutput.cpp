#include "PcmOutput.h"
#include <algorithm>
#include <cmath>
#include <cstring>

QAudioFormat PcmOutput::supportedFormat(const QAudioDevice& device) {
    QAudioFormat result = device.preferredFormat();
    if (!result.isValid() || result.channelCount() < 1 || result.channelCount() > 8 || result.sampleRate() < 8000 || result.sampleRate() > 192000) return {};
    if (result.channelConfig() == QAudioFormat::ChannelConfigUnknown)
        result.setChannelConfig(QAudioFormat::defaultChannelConfigForChannelCount(result.channelCount()));
    for (const auto format : {QAudioFormat::Float, QAudioFormat::Int16, QAudioFormat::Int32, QAudioFormat::UInt8}) {
        result.setSampleFormat(format); if (device.isFormatSupported(result)) return result;
    }
    return {};
}
QByteArray PcmOutput::encode(const float* samples, qsizetype count, QAudioFormat::SampleFormat format) {
    if (!samples || count <= 0 || count > 192000 * 8) return {};
    const int bytes = format == QAudioFormat::UInt8 ? 1 : format == QAudioFormat::Int16 ? 2 : 4;
    QByteArray output(count * bytes, Qt::Uninitialized);
    for (qsizetype i = 0; i < count; ++i) {
        const double sample = std::isfinite(samples[i]) ? std::clamp(double(samples[i]), -1.0, 1.0) : 0;
        auto* target = output.data() + i * bytes;
        if (format == QAudioFormat::Float) { const float value = float(sample); std::memcpy(target, &value, sizeof(value)); }
        else if (format == QAudioFormat::Int16) { const qint16 value = qint16(std::round(sample * (sample < 0 ? 32768 : 32767))); std::memcpy(target, &value, sizeof(value)); }
        else if (format == QAudioFormat::Int32) { const qint32 value = qint32(std::round(sample * (sample < 0 ? 2147483648.0 : 2147483647.0))); std::memcpy(target, &value, sizeof(value)); }
        else if (format == QAudioFormat::UInt8) *target = char(std::round((sample + 1) * 127.5));
        else return {};
    }
    return output;
}
