// RenderPlanBuilder implementation (ROADMAP P2, DESIGN §4.2/§4.3).
//
// The build is deterministic and cheap: one pass over tracks/clips, solo/mute
// resolution, then a PlanClip per audible clip pointing into the arena. The
// GUI holds the arena shared_ptrs in the plan (bufferRefs) so a live/retired
// plan can never outlive the PCM it points at.

#include "core/render/RenderPlanBuilder.h"

#include <QSet>

#include <algorithm>

namespace ses::core {

RenderPlan RenderPlanBuilder::build(const Project& project)
{
    RenderPlan plan;
    plan.sampleRate = project.sampleRate > 0 ? project.sampleRate : 48000;

    // Solo resolution: one soloed track silences every non-soloed track.
    plan.anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                               [](const Track& t) { return t.solo; });

    // Hold the arenas on the GUI side; the plan keeps them until reclaimed.
    // Keyed by path so the same arena is not held twice.
    std::vector<std::shared_ptr<const AudioBuffer>> refs;
    refs.reserve(std::size_t(project.mediaPool.size()));
    QSet<QString> seenPaths;

    for (const Track& track : project.tracks) {
        const bool audible = plan.trackAudible(track);
        for (const Clip& clip : track.clips) {
            if (!audible || clip.muted || clip.length <= 0)
                continue;

            const MediaItem* media = project.mediaById(clip.mediaId);
            if (!media)
                continue;
            std::shared_ptr<const AudioBuffer> buffer;
            if (cache_)
                buffer = cache_->find(media->path);
            if (!buffer || buffer->frames <= 0 || buffer->sampleRate != plan.sampleRate)
                continue; // undecoded or not at the project rate: honest silence

            if (!seenPaths.contains(media->path)) {
                refs.push_back(buffer);
                seenPaths.insert(media->path);
            }

            PlanClip pc;
            pc.samples = buffer->pcm.data();
            pc.arenaFrames = buffer->frames;
            pc.sourceOffset = std::clamp<int64_t>(clip.sourceOffset, 0, buffer->frames);
            pc.timelineStart = std::max<int64_t>(0, clip.timelineStart);
            pc.length = std::clamp<int64_t>(clip.length, 0, buffer->frames - pc.sourceOffset);
            if (pc.length <= 0)
                continue;
            pc.gain = clip.gain;
            pc.pan = track.pan;
            pc.muted = false; // already resolved into `audible`
            pc.fadeIn = clip.fadeIn;
            pc.fadeOut = clip.fadeOut;
            plan.clips.append(std::move(pc));
        }
    }

    // Stable mixer order: by timeline start, then media order for determinism.
    std::stable_sort(plan.clips.begin(), plan.clips.end(),
                     [](const PlanClip& a, const PlanClip& b) {
                         if (a.timelineStart != b.timelineStart)
                             return a.timelineStart < b.timelineStart;
                         return a.samples < b.samples;
                     });

    plan.bufferRefs = std::move(refs);
    return plan;
}

} // namespace ses::core
