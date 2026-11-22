#pragma once
// ses::engine — decode pool (ROADMAP §2.7, P1 task 5). Worker-side decoding
// through ma_decoder (wav/flac/mp3/vorbis) with a libsndfile fallback for
// OGG/Vorbis (reliable length + seek — the stb_vorbis limitation, ROADMAP
// §4), folded to interleaved f32 stereo at the project rate. Waveform peak
// LODs are folded during the same read pass — no second scan (§2.7).
// Allocation is allowed here: none of this code ever runs on the RT thread.
//
// Threading (DESIGN §3): decodeFile/probeFile are synchronous worker helpers;
// DecodePool runs them on a 2-thread QThreadPool and marshals results to the
// GUI thread, where they land in the MediaCache — the GUI never waits on
// decode. The miniaudio header stays out of UI TUs: it is only included by
// Decode.cpp.

#include "core/media/AudioBuffer.h"
#include "core/media/MediaCache.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <memory>

namespace ses::engine {

struct DecodedAudio {
    std::vector<float> pcm; // interleaved stereo f32 @ targetRate
    int sampleRate = 48000;
    int channels = 2;
    int64_t frames = 0;
    core::PeakPyramid peaks;
    QString error; // set when the call returns false
};

// Full decode to interleaved f32 stereo at targetRate (the 48 kHz project
// rate). .ogg/.oga route through libsndfile (§4); everything else through
// ma_decoder with libsndfile as the fallback when ma_decoder rejects the
// file. Files neither library opens are reported honestly in out.error.
bool decodeFile(const QString& path, DecodedAudio& out, int targetRate = 48000);

// Cheap metadata probe (no decode): the source file's rate/channels/frames.
bool probeFile(const QString& path, int& sampleRate, int& channels,
               int64_t& frames, QString& error);

// Async decode pool: fills the GUI-side MediaCache off the GUI thread.
class DecodePool : public QObject {
    Q_OBJECT
public:
    explicit DecodePool(core::MediaCache* cache, int targetRate = 48000,
                        QObject* parent = nullptr);
    ~DecodePool() override;

    // Decode path into the cache. A cached path emits ready() immediately;
    // duplicates already in flight are ignored.
    void request(const QString& path);

    bool busy() const { return !inFlight_.isEmpty(); }

    // Blocks the calling (GUI) thread until every in-flight decode has landed
    // in the cache and its ready()/failed() has been emitted. Tests + shutdown.
    void waitForDone();

signals:
    void ready(const QString& path);
    void failed(const QString& path, const QString& error);

private:
    void finish(const QString& path, const std::shared_ptr<core::AudioBuffer>& buffer,
                const QString& error);

    core::MediaCache* cache_ = nullptr;
    int targetRate_ = 48000;
    QThreadPool pool_;
    QSet<QString> inFlight_;
};

} // namespace ses::engine