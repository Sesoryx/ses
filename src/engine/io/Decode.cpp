// Decode pool implementation (ROADMAP §2.7/§4, P1 task 5). The only engine
// TU besides AudioDevice.cpp that includes the miniaudio header; libsndfile
// covers what ma_decoder cannot (OGG/Vorbis length + seek). Every function
// here runs on a decode worker — allocation and blocking are fine, the RT
// rules (DESIGN §2) do not apply.

#include "engine/io/Decode.h"

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

// ma_decoder path (ROADMAP §2.7): wav/flac/mp3/vorbis built in, output
// converted to f32 stereo at targetRate by the decoder's internal resampler.
bool decodeViaMaDecoder(const QString& path, DecodedAudio& out, int targetRate,
                        core::PeakBuilder& peaks)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, ma_uint32(targetRate));
    ma_decoder decoder;
    const QByteArray utf8 = path.toUtf8();
    if (ma_decoder_init_file(utf8.constData(), &cfg, &decoder) != MA_SUCCESS) {
        out.error = QStringLiteral("unrecognized or corrupt audio (miniaudio decoder)");
        return false;
    }

    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total);
    // CAUTION: total comes back 0 for Vorbis (stb_vorbis push mode, §2.10) —
    // reserve when known, grow while reading otherwise.
    if (total > 0)
        out.pcm.reserve(static_cast<std::size_t>(total) * 2);

    float buf[kDecodeChunkFrames * 2];
    ma_uint64 read = 0;
    while (ma_decoder_read_pcm_frames(&decoder, buf, kDecodeChunkFrames, &read) == MA_SUCCESS
           && read > 0) {
        const std::size_t base = out.pcm.size();
        out.pcm.resize(base + static_cast<std::size_t>(read) * 2);
        for (ma_uint64 i = 0; i < read * 2; ++i)
            out.pcm[base + i] = decodeCleanSample(buf[i]);
        decodeFoldPeaks(peaks, out.pcm.data() + base, static_cast<std::size_t>(read));
    }
    ma_decoder_uninit(&decoder); // returns ma_result here (§2.10)

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
// linear ma_resampler to targetRate (§2.8 "simple internal conversions";
// task 6 swaps in soxr VHQ for import quality). sf_readf_float normalizes
// every container into [-1,1] f32.
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

    const bool resample = info.samplerate != targetRate;
    ma_resampler resampler{};
    if (resample) {
        const ma_resampler_config rc = ma_resampler_config_init(
            ma_format_f32, 2, ma_uint32(info.samplerate), ma_uint32(targetRate),
            ma_resample_algorithm_linear);
        if (ma_resampler_init(&rc, nullptr, &resampler) != MA_SUCCESS) {
            out.error = QStringLiteral("cannot set up sample rate conversion");
            sf_close(file);
            return false;
        }
    }

    const int channels = info.channels;
    std::vector<float> in(static_cast<std::size_t>(kDecodeChunkFrames) * channels);
    std::vector<float> stereo(static_cast<std::size_t>(kDecodeChunkFrames) * 2);
    std::vector<float> converted(
        static_cast<std::size_t>(std::ceil(double(kDecodeChunkFrames)
                                           * double(targetRate) / double(info.samplerate)))
        + 256);

    sf_count_t got = 0;
    while ((got = sf_readf_float(file, in.data(), kDecodeChunkFrames)) > 0) {
        // Fold to stereo: mono duplicates, multi-channel takes the front pair.
        for (sf_count_t i = 0; i < got; ++i) {
            const float left = channels == 1 ? in[static_cast<std::size_t>(i)]
                                             : in[static_cast<std::size_t>(i) * channels];
            const float right = channels == 1 ? in[static_cast<std::size_t>(i)]
                                              : in[static_cast<std::size_t>(i) * channels + 1];
            stereo[static_cast<std::size_t>(i) * 2] = decodeCleanSample(left);
            stereo[static_cast<std::size_t>(i) * 2 + 1] = decodeCleanSample(right);
        }

        std::size_t done = 0;
        const std::size_t framesIn = static_cast<std::size_t>(got);
        while (done < framesIn) {
            const float* src = stereo.data() + done * 2;
            ma_uint64 inFrames = ma_uint64(framesIn - done);
            if (!resample) {
                const std::size_t base = out.pcm.size();
                out.pcm.resize(base + static_cast<std::size_t>(inFrames) * 2);
                std::copy_n(src, static_cast<std::size_t>(inFrames) * 2, out.pcm.data() + base);
                decodeFoldPeaks(peaks, out.pcm.data() + base, static_cast<std::size_t>(inFrames));
                done = framesIn;
                break;
            }
            ma_uint64 outFrames = ma_uint64(converted.size() / 2);
            if (ma_resampler_process_pcm_frames(&resampler, src, &inFrames, converted.data(),
                                               &outFrames)
                != MA_SUCCESS) {
                out.error = QStringLiteral("sample rate conversion failed");
                ma_resampler_uninit(&resampler, nullptr);
                sf_close(file);
                return false;
            }
            const std::size_t base = out.pcm.size();
            out.pcm.resize(base + static_cast<std::size_t>(outFrames) * 2);
            std::copy_n(converted.data(), static_cast<std::size_t>(outFrames) * 2,
                        out.pcm.data() + base);
            decodeFoldPeaks(peaks, out.pcm.data() + base, static_cast<std::size_t>(outFrames));
            if (inFrames == 0)
                break; // no progress — do not spin
            done += static_cast<std::size_t>(inFrames);
        }
    }
    if (resample)
        ma_resampler_uninit(&resampler, nullptr);
    sf_close(file);

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