#include "inference/Catalog.h"

namespace ses::inference {

QString capabilityName(Capability c)
{
    switch (c) {
    case Capability::Vad: return QStringLiteral("VAD");
    case Capability::Asr: return QStringLiteral("Transcribe");
    case Capability::Tts: return QStringLiteral("Speech");
    case Capability::Enhance: return QStringLiteral("Enhance");
    case Capability::SuperRes: return QStringLiteral("Super-res");
    case Capability::VoiceConversion: return QStringLiteral("Voice");
    case Capability::Music: return QStringLiteral("Music");
    }
    return {};
}

const QVector<CatalogEntry>& catalog()
{
    // Sizes/licenses verified against upstream docs 2026-11-20 (DESIGN.md §0/§7.6).
    static const QVector<CatalogEntry> entries = {
        { Capability::Vad, QStringLiteral("Silero VAD"), QStringLiteral("silero_vad"),
          QStringLiteral("~1–2 M"), QStringLiteral("MIT"), QStringLiteral("bundled"),
          QStringLiteral("Ships with the app; no download."), true, false },
        { Capability::Asr, QStringLiteral("Niagara 19M Batch"), QStringLiteral("niagara_asr"),
          QStringLiteral("19 M"), QStringLiteral("ABR Open License v1.1"), QStringLiteral("≈76 MB f32"),
          QStringLiteral("English only. Free under $1M yearly revenue; otherwise research-only."), false, false },
        { Capability::Asr, QStringLiteral("SenseVoice-Small"), QStringLiteral("sense_asr"),
          QStringLiteral("≈244 M"), QStringLiteral("FunASR Model License"), QStringLiteral("large"),
          QStringLiteral("Turkish-capable ASR exception — no million-scale Turkish ASR exists. Credit required."), false, true },
        { Capability::Tts, QStringLiteral("sanoTTS tr"), QStringLiteral("sanotts_tr_orig"),
          QStringLiteral("1.56 M"), QStringLiteral("GPL-3.0"), QStringLiteral("≈6 MB f32"),
          QStringLiteral("Turkish voice. Copyleft: opt-in download with license warning. Needs eSpeak-ng tr data."), false, true },
        { Capability::Tts, QStringLiteral("tiny-voca"), QStringLiteral("tiny_voca_orig"),
          QStringLiteral("4.5 M"), QStringLiteral("Apache-2.0"), QStringLiteral("≈18 MB f32"),
          QStringLiteral("English, smallest permissive voice."), false, false },
        { Capability::Tts, QStringLiteral("PocketTTS Small 2M"), QStringLiteral("pocket_tts_small_english_2m_f32"),
          QStringLiteral("2 M"), QStringLiteral("CC-BY-4.0"), QStringLiteral("≈8 MB f32"),
          QStringLiteral("English. Attribution required."), false, false },
        { Capability::Enhance, QStringLiteral("GTCRN / RNNoise / DFN2"), QStringLiteral("builtin_audio_utils"),
          QStringLiteral("sub-M … ~2 M"), QStringLiteral("BSD-3 / MIT / Apache-2.0"), QStringLiteral("<1 MB … ~8 MB"),
          QStringLiteral("Denoise/enhancement; instant on CPU."), false, false },
        { Capability::SuperRes, QStringLiteral("FlashSR (light)"), QStringLiteral("builtin_audio_utils"),
          QStringLiteral("small (enhancer-only)"), QStringLiteral("see upstream"), QStringLiteral("small"),
          QStringLiteral("Default light SR path. LavaSR enhancer port is tracked separately; labeled experimental until parity-validated."), false, false },
        { Capability::SuperRes, QStringLiteral("MossFormer2 SR 48K"), QStringLiteral("mossformer2_sr_48k_f16"),
          QStringLiteral("not published (larger)"), QStringLiteral("Apache-2.0"), QStringLiteral("heavier"),
          QStringLiteral("Quality alternate, not the default."), false, true },
        { Capability::VoiceConversion, QStringLiteral("MeanVC2 / OpenVoiceV2"), QStringLiteral("meanvc2_120ms_40ms_f32"),
          QStringLiteral("tens–hundreds M"), QStringLiteral("Apache-2.0 / MIT"), QStringLiteral("large"),
          QStringLiteral("No million-scale VC model exists. Opt-in, not installed by default."), false, true },
        { Capability::Music, QStringLiteral("Magenta RealTime 2 Small"), QStringLiteral("magenta_realtime_2"),
          QStringLiteral("230 M"), QStringLiteral("CC-BY-4.0"), QStringLiteral("≈0.5–1 GB"),
          QStringLiteral("Smallest music model available. Slow on this CPU — calibrated ETA shown. Opt-in."), false, true },
    };
    return entries;
}

} // namespace ses::inference