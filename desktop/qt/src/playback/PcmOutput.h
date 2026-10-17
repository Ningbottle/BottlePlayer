#pragma once
#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>

namespace PcmOutput {
QAudioFormat supportedFormat(const QAudioDevice& device);
QByteArray encode(const float* samples, qsizetype count, QAudioFormat::SampleFormat format);
}
