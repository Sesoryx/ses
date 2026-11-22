// Decode pool implementation (ROADMAP §2.7/§4, P1 task 5). The only engine
// TU besides AudioDevice.cpp that includes the miniaudio header; libsndfile
// covers what ma_decoder cannot (OGG/Vorbis length + seek). Every function
// here runs on a decode worker — allocation and blocking are fine, the RT
// rules (DESIGN §2) do not apply.

#include "engine/io/Decode.h"

#include "engine/dsp/Resampler.h"

#include <miniaudio.h>
#include <sndfile.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ses::engine {

namespace {

constexpr ma_uint32 kDecodeChunkFrames = 4096;

// Non-finite samples (hostile float WAVs) never reach the arena or the peaks.
float decodeCleanSample(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}

bool decodeIsSndfileRoute(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("ogg") || suffix == QLatin1String("oga");
}

void decodeFoldPeaks(core::PeakBuilder& peaks, const float* interleaved, std::size_t frames)
{
    for (std::size_t i = 0; i < frames; ++i)
        peaks.pushFrame(interleaved[i * 2], interleaved[i * 2 + 1]);
}

// Scratch for one decode chunk after import SRC: worst-case output frames
// plus margin. Peak LODs are folded on the converted samples, matching the
// project-rate arena bucket for bucket (§2.7).
std::size_t convertScratchFrames(int nativeRate, int targetRate)
{
    const double ratio = double(targetRate) / double(nativeRate);
    return std::size_t(std::ceil(double(kDecodeChunkFrames) * ratio)) + 256;
}

void appendConverted(DecodedAudio& out, core::PeakBuilder& peaks,
                     const float* data, std::size_t frames)
{
    const std::size_t base = out.pcm.size();
    out.pcm.resize(base + frames * 2);
    std::copy_n(data, frames * 2, out.pcm.data() + base);
    decodeFoldPeaks(peaks, out.pcm.data() + base, frames);
}

// Native-rate interleaved stereo chunk → soxr VHQ → out.pcm at the target
// rate (ROADMAP §3.2: import quality). Partial consumes are looped until the
// chunk is eaten. False when the resampler makes no progress.
bool convertPush(Resampler& rs, const float* stereo, std::size_t frames,
                 std::vector<float>& scratch, DecodedAudio& out,
                 core::PeakBuilder& peaks)
{
    std::size_t done = 0;
    int guard = 0;
    while (done < frames && guard++ < 1024) {
        std::size_t consumed = 0;
        const std::size_t made = rs.process(stereo + done * 2, frames - done,
                                            scratch.data(), scratch.size() / 2,
                                            consumed);
        if (made > 0)
            appendConverted(out, peaks, scratch.data(), made);
        done += consumed;
        if (consumed == 0 && made == 0)
            return false; // stuck — report honestly
    }
    return done == frames;
}

// Drains the queued tail after the last chunk (end of input, §3.2).
bool convertFlush(Resampler& rs, std::vector<float>& scratch, DecodedAudio& out,
                  core::PeakBuilder& peaks)
{
    for (int guard = 0; guard < 1024; ++guard) {
        std::size_t consumed = 0;
        const std::size_t made = rs.process(nullptr, 0, scratch.data(),
                                            scratch.size() / 2, consumed);
        if (made == 0)
            return true;
        appendConverted(out, peaks, scratch.data(), made);
    }
    return false; // the flush never drained — pathological
}

// ma_decoder path (ROADMAP §2.7): wav/flac/mp3/vorbis built in. Decoded at
// the file's native rate; the import SRC to targetRate is soxr VHQ (§2.8 —
// import quality matters), not the decoder's linear internal resampler.
bool decodeViaMaDecoder(const QString& path, DecodedAudio& out, int targetRate,
                        core::PeakBuilder& peaks)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 0); // native rate
    ma_decoder decoder;
    const QByteArray utf8 = path.toUtf8();
    if (ma_decoder_init_file(utf8.constData(), &cfg, &decoder) != MA_SUCCESS) {
        out.error = QStringLiteral("unrecognized or corrupt audio (miniaudio decoder)");
        return false;
    }
    const int nativeRate = static_cast<int>(decoder.outputSampleRate);
    if (nativeRate <= 0) {
        out.error = QStringLiteral("unknown source sample rate");
        ma_decoder_uninit(&decoder);
        return false;
    }

    Resampler rs;
    if (!rs.init(nativeRate, targetRate, 2, Resampler::Quality::Offline)) {
        out.error = QStringLiteral("cannot set up sample rate conversion");
        ma_decoder_uninit(&decoder);
        return false;
    }
    std::vector<float> scratch(convertScratchFrames(nativeRate, targetRate) * 2);

    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total);
    // CAUTION: total comes back 0 for Vorbis (stb_vorbis push mode, §2.10) —
    // reserve when known, grow while reading otherwise.
    if (total > 0) {
        out.pcm.reserve(static_cast<std::size_t>(
                            std::ceil(double(total) * double(targetRate) / double(nativeRate)))
                            * 2
                        + 2);
    }

    float buf[kDecodeChunkFrames * 2];
    ma_uint64 read = 0;
    bool ok = true;
    while (ok && ma_decoder_read_pcm_frames(&decoder, buf, kDecodeChunkFrames, &read) == MA_SUCCESS
           && read > 0) {
        for (ma_uint64 i = 0; i < read * 2; ++i)
            buf[i] = decodeCleanSample(buf[i]);
        ok = convertPush(rs, buf, static_cast<std::size_t>(read), scratch, out, peaks);
    }
    ma_decoder_uninit(&decoder); // returns ma_result here (§2.10)

    if (!ok || !convertFlush(rs, scratch, out, peaks)) {
        out.error = QStringLiteral("sample rate conversion failed");
        return false;
    }

    out.sampleRate = targetRate;
    out.channels = 2;
    out.frames = static_cast<int64_t>(out.pcm.size() / 2);
    if (out.frames <= 0) {
        out.error = QStringLiteral("no audio frames decoded");
        return false;
    }
    return true;
}

// libsndfile path (ROADMAP §4): native-rate interleaved f32 → stereo fold →
// soxr VHQ to targetRate (§2.8 "media import" — import quality matters, not
// speed; P1 task 6). sf_readf_float normalizes every container into [-1,1] f32.
bool decodeViaSndfile(const QString& path, DecodedAudio& out, int targetRate,
                      core::PeakBuilder& peaks)
{
    SF_INFO info{}; // zero-init is mandatory
    const QByteArray utf8 = path.toUtf8();
    SNDFILE* file = sf_open(utf8.constData(), SFM_READ, &info);
    if (!file) {
        out.error = QString::fromUtf8(sf_strerror(nullptr));
        return false;
    }
    if (info.channels <= 0 || info.samplerate <= 0) {
        out.error = QStringLiteral("unsupported channel layout or sample rate");
        sf_close(file);
        return false;
    }

    Resampler rs;
    if (!rs.init(info.samplerate, targetRate, 2, Resampler::Quality::Offline)) {
        out.error = QStringLiteral("cannot set up sample rate conversion");
        sf_close(file);
        return false;
    }
    std::vector<float> scratch(convertScratchFrames(info.samplerate, targetRate) * 2);
    if (info.frames > 0) {
        out.pcm.reserve(static_cast<std::size_t>(
                            std::ceil(double(info.frames) * double(targetRate)
                                      / double(info.samplerate)))
                            * 2
                        + 2);
    }

    const int channels = info.channels;
    std::vector<float> in(static_cast<std::size_t>(kDecodeChunkFrames) * channels);
    std::vector<float> stereo(static_cast<std::size_t>(kDecodeChunkFrames) * 2);

    sf_count_t got = 0;
    bool ok = true;
    while (ok && (got = sf_readf_float(file, in.data(), kDecodeChunkFrames)) > 0) {
        // Fold to stereo: mono duplicates, multi-channel takes the front pair.
        for (sf_count_t i = 0; i < got; ++i) {
            const float left = channels == 1 ? in[static_cast<std::size_t>(i)]
                                             : in[static_cast<std::size_t>(i) * channels];
            const float right = channels == 1 ? in[static_cast<std::size_t>(i)]
                                              : in[static_cast<std::size_t>(i) * channels + 1];
            stereo[static_cast<std::size_t>(i) * 2] = decodeCleanSample(left);
            stereo[static_cast<std::size_t>(i) * 2 + 1] = decodeCleanSample(right);
        }
        ok = convertPush(rs, stereo.data(), static_cast<std::size_t>(got), scratch, out, peaks);
    }
    sf_close(file);

    if (!ok || !convertFlush(rs, scratch, out, peaks)) {
        out.error = QStringLiteral("sample rate conversion failed");
        return false;
    }

    out.sampleRate = targetRate;
    out.channels = 2;
    out.frames = static_cast<int64_t>(out.pcm.size() / 2);
    if (out.frames <= 0) {
        out.error = QStringLiteral("no audio frames decoded");
        return false;
    }
    return true;
}

} // namespace

bool decodeFile(const QString& path, DecodedAudio& out, int targetRate)
{
    out = DecodedAudio{};
    if (targetRate <= 0) {
        out.error = QStringLiteral("invalid target sample rate");
        return false;
    }
    out.sampleRate = targetRate;

    using DecodeFn = bool (*)(const QString&, DecodedAudio&, int, core::PeakBuilder&);
    const DecodeFn primary = decodeIsSndfileRoute(path) ? &decodeViaSndfile : &decodeViaMaDecoder;
    const DecodeFn fallback = decodeIsSndfileRoute(path) ? &decodeViaMaDecoder : &decodeViaSndfile;

    core::PeakBuilder peaks;
    if (primary(path, out, targetRate, peaks)) {
        out.peaks = peaks.finish();
        return true;
    }

    // §4: one library's rejection is not the file's fault — try the other.
    const QString firstError = out.error;
    out = DecodedAudio{};
    out.sampleRate = targetRate;
    core::PeakBuilder retryPeaks;
    if (fallback(path, out, targetRate, retryPeaks)) {
        out.peaks = retryPeaks.finish();
        return true;
    }
    if (out.error.isEmpty())
        out.error = firstError.isEmpty() ? QStringLiteral("unsupported or corrupt audio file")
                                         : firstError;
    return false;
}

bool probeFile(const QString& path, int& sampleRate, int& channels, int64_t& frames,
               QString& error)
{
    sampleRate = 0;
    channels = 0;
    frames = 0;
    error.clear();

    SF_INFO info{}; // zero-init is mandatory
    const QByteArray utf8 = path.toUtf8();
    if (SNDFILE* file = sf_open(utf8.constData(), SFM_READ, &info)) {
        sampleRate = info.samplerate;
        channels = info.channels;
        frames = static_cast<int64_t>(info.frames); // reliable even for OGG (§4)
        sf_close(file);
        if (sampleRate > 0 && channels > 0 && frames > 0)
            return true;
    }

    // Fallback for files sndfile cannot open: ma_decoder's view (the frame
    // count is 0 for Vorbis — the documented stb_vorbis limitation, §2.10).
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(utf8.constData(), &cfg, &decoder) != MA_SUCCESS) {
        error = QStringLiteral("unsupported or corrupt audio file");
        return false;
    }
    sampleRate = static_cast<int>(decoder.outputSampleRate);
    channels = static_cast<int>(decoder.outputChannels);
    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total);
    frames = static_cast<int64_t>(total);
    ma_decoder_uninit(&decoder);
    return sampleRate > 0 && channels > 0;
}

DecodePool::DecodePool(core::MediaCache* cache, int targetRate, QObject* parent)
    : QObject(parent)
    , cache_(cache)
    , targetRate_(targetRate > 0 ? targetRate : 48000)
{
    pool_.setMaxThreadCount(2); // DESIGN §3: decode pool, two workers
}

DecodePool::~DecodePool()
{
    // Workers must be done before the object goes away; queued completions
    // still pending on this context are dropped by ~QObject.
    pool_.waitForDone();
}

void DecodePool::request(const QString& path)
{
    if (cache_ && cache_->contains(path)) {
        emit ready(path); // already decoded — answer immediately
        return;
    }
    if (inFlight_.contains(path))
        return;
    inFlight_.insert(path);

    pool_.start([this, path] {
        auto buffer = std::make_shared<core::AudioBuffer>();
        DecodedAudio decoded;
        QString error;
        if (decodeFile(path, decoded, targetRate_)) {
            buffer->pcm = std::move(decoded.pcm);
            buffer->sampleRate = decoded.sampleRate;
            buffer->channels = decoded.channels;
            buffer->frames = decoded.frames;
            buffer->peaks = std::move(decoded.peaks);
        } else {
            error = decoded.error.isEmpty() ? QStringLiteral("decode failed") : decoded.error;
        }
        // Marshal to the GUI thread: the cache and the signals belong to it.
        QMetaObject::invokeMethod(
            this, [this, path, buffer, error] { finish(path, buffer, error); },
            Qt::QueuedConnection);
    });
}

void DecodePool::waitForDone()
{
    pool_.waitForDone();
    // Deliver the completions already posted to this object so the cache is
    // fully landed when we return.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

void DecodePool::finish(const QString& path, const std::shared_ptr<core::AudioBuffer>& buffer,
                        const QString& error)
{
    inFlight_.remove(path);
    if (buffer && buffer->frames > 0) {
        if (cache_)
            cache_->insert(path, buffer);
        emit ready(path);
    } else {
        emit failed(path, error);
    }
}

} // namespace ses::engine