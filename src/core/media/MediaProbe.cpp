#include "core/media/MediaProbe.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ses::core {

namespace {

struct Chunk {
    char id[5] = {};
    uint32_t size = 0;
    int64_t dataPos = 0;
};

// Iterates RIFF chunks; returns false at EOF/malformed.
class RiffReader {
public:
    explicit RiffReader(QFile& f)
        : f_(f)
    {
        char header[12];
        if (f.read(header, 12) != 12 || std::memcmp(header, "RIFF", 4) != 0
            || std::memcmp(header + 8, "WAVE", 4) != 0)
            return;
        valid_ = true;
        pos_ = 12;
        fileSize_ = f.size();
    }
    bool valid() const { return valid_; }

    bool next(Chunk& c)
    {
        if (!valid_ || pos_ + 8 > fileSize_)
            return false;
        f_.seek(pos_);
        char hdr[8];
        if (f_.read(hdr, 8) != 8)
            return false;
        std::memcpy(c.id, hdr, 4);
        c.id[4] = 0;
        c.size = qFromLittleEndian<uint32_t>(hdr + 4);
        c.dataPos = pos_ + 8;
        pos_ = c.dataPos + c.size + (c.size & 1); // chunks are word-aligned
        return true;
    }

private:
    QFile& f_;
    bool valid_ = false;
    int64_t pos_ = 0;
    int64_t fileSize_ = 0;
};

struct FmtInfo {
    uint16_t format = 0; // 1 PCM int, 3 IEEE float, 0xFFFE extensible
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bits = 0;
};

bool parseFmt(QFile& f, const Chunk& c, FmtInfo& fmt)
{
    if (c.size < 16)
        return false;
    f.seek(c.dataPos);
    QByteArray b = f.read(16);
    if (b.size() != 16)
        return false;
    const auto* u = reinterpret_cast<const uchar*>(b.constData());
    fmt.format = qFromLittleEndian<uint16_t>(u);
    fmt.channels = qFromLittleEndian<uint16_t>(u + 2);
    fmt.sampleRate = qFromLittleEndian<uint32_t>(u + 4);
    fmt.blockAlign = qFromLittleEndian<uint16_t>(u + 12);
    fmt.bits = qFromLittleEndian<uint16_t>(u + 14);
    if (fmt.format == 0xFFFE && c.size >= 40) {
        // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID's first two bytes = real format.
        f.seek(c.dataPos + 24);
        QByteArray sub = f.read(2);
        if (sub.size() == 2)
            fmt.format = qFromLittleEndian<uint16_t>(reinterpret_cast<const uchar*>(sub.constData()));
    }
    return fmt.channels > 0 && fmt.sampleRate > 0 && fmt.blockAlign > 0;
}

// Reads one sample of channel ch at frame index, normalized to [-1,1].
double readSample(const QByteArray& frame, const FmtInfo& fmt, int ch)
{
    const int bytes = fmt.bits / 8;
    const uchar* p = reinterpret_cast<const uchar*>(frame.constData()) + ch * bytes;
    if (fmt.format == 3) {
        if (fmt.bits == 32) {
            const uint32_t u = qFromLittleEndian<uint32_t>(p);
            float v;
            std::memcpy(&v, &u, 4);
            return v;
        }
        if (fmt.bits == 64) {
            const uint64_t u = qFromLittleEndian<uint64_t>(p);
            double v;
            std::memcpy(&v, &u, 8);
            return v;
        }
        return 0.0;
    }
    switch (fmt.bits) {
    case 8: return (double(p[0]) - 128.0) / 128.0; // unsigned
    case 16: return double(qFromLittleEndian<int16_t>(p)) / 32768.0;
    case 24: {
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (v & 0x800000)
            v |= int32_t(0xFF000000); // sign extend
        return double(v) / 8388608.0;
    }
    case 32: return double(qFromLittleEndian<int32_t>(p)) / 2147483648.0;
    default: return 0.0;
    }
}

} // namespace

MediaInfo probeWav(const QString& path)
{
    MediaInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("Cannot open file");
        return info;
    }
    RiffReader riff(f);
    if (!riff.valid()) {
        info.error = QStringLiteral("Not a RIFF/WAVE file");
        return info;
    }
    FmtInfo fmt;
    bool haveFmt = false;
    int64_t dataSize = -1;
    int64_t factFrames = -1;
    Chunk c;
    while (riff.next(c)) {
        if (std::strcmp(c.id, "fmt ") == 0) {
            haveFmt = parseFmt(f, c, fmt);
        } else if (std::strcmp(c.id, "data") == 0) {
            dataSize = c.size;
        } else if (std::strcmp(c.id, "fact") == 0 && c.size >= 4) {
            f.seek(c.dataPos);
            QByteArray b = f.read(4);
            if (b.size() == 4)
                factFrames = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(b.constData()));
        }
    }
    if (!haveFmt || dataSize < 0) {
        info.error = QStringLiteral("Missing fmt/data chunk");
        return info;
    }
    if (fmt.format != 1 && fmt.format != 3) {
        info.error = QStringLiteral("Compressed WAV (format %1) — decode arrives with the audio engine").arg(fmt.format);
        return info;
    }
    if (fmt.bits != 8 && fmt.bits != 16 && fmt.bits != 24 && fmt.bits != 32
        && !(fmt.format == 3 && fmt.bits == 64)) {
        info.error = QStringLiteral("Unsupported bit depth %1").arg(fmt.bits);
        return info;
    }
    info.ok = true;
    info.format = QStringLiteral("wav");
    info.sampleRate = int(fmt.sampleRate);
    info.channels = fmt.channels;
    info.bitsPerSample = fmt.bits;
    info.isFloat = fmt.format == 3;
    info.frames = factFrames >= 0 ? factFrames : dataSize / fmt.blockAlign;
    return info;
}

Peaks computeWavPeaks(const QString& path, int buckets)
{
    Peaks peaks;
    const MediaInfo info = probeWav(path);
    if (!info.ok || info.frames <= 0 || buckets <= 0)
        return peaks;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return peaks;
    RiffReader riff(f);
    FmtInfo fmt;
    int64_t dataPos = -1, dataSize = 0;
    Chunk c;
    while (riff.next(c)) {
        if (std::strcmp(c.id, "fmt ") == 0)
            parseFmt(f, c, fmt);
        else if (std::strcmp(c.id, "data") == 0) {
            dataPos = c.dataPos;
            dataSize = c.size;
        }
    }
    if (dataPos < 0 || fmt.blockAlign == 0)
        return peaks;

    const int64_t frames = std::min<int64_t>(info.frames, dataSize / fmt.blockAlign);
    const int64_t perBucket = std::max<int64_t>(1, frames / buckets);
    const int nBuckets = int((frames + perBucket - 1) / perBucket);
    peaks.min.resize(nBuckets);
    peaks.max.resize(nBuckets);
    peaks.samplesPerBucket = perBucket;

    f.seek(dataPos);
    const int64_t chunkFrames = 4096;
    QByteArray buf;
    buf.resize(int(chunkFrames * fmt.blockAlign));
    int64_t frame = 0;
    while (frame < frames) {
        const int64_t want = std::min(chunkFrames, frames - frame);
        const qint64 got = f.read(buf.data(), want * fmt.blockAlign);
        if (got <= 0)
            break;
        const int64_t gotFrames = got / fmt.blockAlign;
        for (int64_t i = 0; i < gotFrames; ++i) {
            const int64_t abs = frame + i;
            const int b = int(abs / perBucket);
            const QByteArray frameData = QByteArray::fromRawData(buf.constData() + i * fmt.blockAlign, fmt.blockAlign);
            double mix = 0.0;
            for (int ch = 0; ch < fmt.channels; ++ch)
                mix += readSample(frameData, fmt, ch);
            mix /= fmt.channels;
            const float v = float(mix);
            if (abs % perBucket == 0) {
                peaks.min[b] = v;
                peaks.max[b] = v;
            } else {
                peaks.min[b] = std::min(peaks.min[b], v);
                peaks.max[b] = std::max(peaks.max[b], v);
            }
        }
        frame += gotFrames;
    }
    return peaks;
}

bool writeTestWav(const QString& path, double seconds, int sampleRate, int channels)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const int64_t frames = int64_t(seconds * sampleRate);
    const uint16_t bits = 16;
    const uint16_t blockAlign = uint16_t(channels * bits / 8);
    const uint32_t dataSize = uint32_t(frames * blockAlign);

    auto put16 = [&f](uint16_t v) {
        char b[2];
        qToLittleEndian(v, b);
        f.write(b, 2);
    };
    auto put32 = [&f](uint32_t v) {
        char b[4];
        qToLittleEndian(v, b);
        f.write(b, 4);
    };

    f.write("RIFF", 4);
    put32(36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    put32(16);
    put16(1); // PCM
    put16(uint16_t(channels));
    put32(uint32_t(sampleRate));
    put32(uint32_t(sampleRate * blockAlign));
    put16(blockAlign);
    put16(bits);
    f.write("data", 4);
    put32(dataSize);

    QByteArray buf;
    buf.resize(int(std::min<int64_t>(frames, 4096) * blockAlign));
    for (int64_t i = 0; i < frames; ++i) {
        const double t = double(i) / sampleRate;
        // Gentle sweep 220→880 Hz with a slow tremolo — visible waveform shape.
        const double phase = 2.0 * M_PI * (220.0 * t + 220.0 * t * t / (2.0 * seconds));
        const double amp = 0.55 * (0.6 + 0.4 * std::sin(2.0 * M_PI * 1.5 * t));
        const auto s = int16_t(std::clamp(std::sin(phase) * amp, -1.0, 1.0) * 32767.0);
        const int64_t off = (i % 4096) * blockAlign;
        for (int ch = 0; ch < channels; ++ch)
            qToLittleEndian(s, buf.data() + off + ch * 2);
        if ((i % 4096) == 4095 || i == frames - 1)
            f.write(buf.constData(), int(((i % 4096) + 1) * blockAlign));
    }
    return f.error() == QFile::NoError;
}

} // namespace ses::core