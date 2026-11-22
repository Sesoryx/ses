#include "core/media/MediaProbe.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
        const int64_t riffSize = qFromLittleEndian<uint32_t>(header + 4);
        const int64_t riffEnd = 8 + riffSize;
        if (riffSize < 4 || riffEnd > f.size())
            return;
        valid_ = true;
        pos_ = 12;
        fileSize_ = riffEnd;
    }
    bool valid() const { return valid_; }

    bool next(Chunk& c)
    {
        if (!valid_ || pos_ == fileSize_)
            return false;
        if (fileSize_ - pos_ < 8 || !f_.seek(pos_)) {
            valid_ = false;
            return false;
        }
        char hdr[8];
        if (f_.read(hdr, 8) != 8) {
            valid_ = false;
            return false;
        }
        std::memcpy(c.id, hdr, 4);
        c.id[4] = 0;
        c.size = qFromLittleEndian<uint32_t>(hdr + 4);
        c.dataPos = pos_ + 8;
        const int64_t paddedSize = int64_t(c.size) + (c.size & 1);
        if (paddedSize > fileSize_ - c.dataPos) {
            valid_ = false;
            return false;
        }
        pos_ = c.dataPos + paddedSize; // chunks are word-aligned
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
    if (!riff.valid()) {
        info.error = QStringLiteral("Malformed or truncated WAV chunk");
        return info;
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
    if ((fmt.format == 1 && fmt.bits == 64) || (fmt.format == 3 && fmt.bits == 8)) {
        info.error = QStringLiteral("Unsupported WAV encoding and bit-depth combination");
        return info;
    }
    const int bytesPerSample = fmt.bits / 8;
    if (fmt.blockAlign != fmt.channels * bytesPerSample) {
        info.error = QStringLiteral("Invalid WAV block alignment");
        return info;
    }
    if (dataSize % fmt.blockAlign != 0) {
        info.error = QStringLiteral("WAV data size is not frame-aligned");
        return info;
    }
    const int64_t dataFrames = dataSize / fmt.blockAlign;
    if (factFrames > dataFrames) {
        info.error = QStringLiteral("WAV frame count exceeds available audio data");
        return info;
    }
    info.ok = true;
    info.format = QStringLiteral("wav");
    info.sampleRate = int(fmt.sampleRate);
    info.channels = fmt.channels;
    info.bitsPerSample = fmt.bits;
    info.isFloat = fmt.format == 3;
    info.frames = factFrames >= 0 ? factFrames : dataFrames;
    return info;
}

bool writeTestWav(const QString& path, double seconds, int sampleRate, int channels)
{
    if (!std::isfinite(seconds) || seconds <= 0.0 || sampleRate <= 0
        || channels <= 0 || channels > 2)
        return false;
    const long double requestedFrames = static_cast<long double>(seconds) * sampleRate;
    const uint16_t bits = 16;
    const uint16_t blockAlign = uint16_t(channels * bits / 8);
    const uint64_t maxDataSize = std::numeric_limits<uint32_t>::max() - 36;
    if (requestedFrames < 1.0L
        || uint64_t(sampleRate) > std::numeric_limits<uint32_t>::max() / blockAlign
        || requestedFrames > static_cast<long double>(maxDataSize / blockAlign))
        return false;
    const int64_t frames = int64_t(requestedFrames);
    const uint32_t dataSize = uint32_t(frames * blockAlign);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;

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