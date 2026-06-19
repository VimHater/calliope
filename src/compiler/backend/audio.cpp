#include "backend/audio.hpp"

#include "backend/score.hpp"

#include "miniaudio.h" // no MINIAUDIO_IMPLEMENTATION here (see miniaudio_impl.cpp)
#include "sfizz.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace calliope::backend {

namespace {

constexpr int BLOCK = 512; // frames per sfizz render call

// A note-on or note-off scheduled at an absolute sample position.
struct SampleEv { long long sample; bool on; int key; };

// Whole-note time -> sample index. seconds = r * (4*60/bpm); samples = seconds * sr.
// Done in one integer expression (round to nearest) to avoid float drift.
long long to_sample(const Rational& r, int sample_rate, int bpm) {
    long long num = r.num * 240LL * sample_rate; // 240 = 4 beats * 60 s
    long long den = r.den * static_cast<long long>(bpm);
    return (num + den / 2) / den;
}

} // namespace

bool write_wav(const music::Music& m, music::MusicId root,
               const AudioOptions& opt, const std::string& path, std::string& err) {
    if (opt.sfz_path.empty()) { err = "no soundfont given"; return false; }

    // Music IR -> absolute-timed notes -> sample-positioned on/off events.
    std::vector<TimedNote> notes = flatten(m, root);
    std::vector<SampleEv> evs;
    evs.reserve(notes.size() * 2);
    long long last_end = 0;
    for (const TimedNote& n : notes) {
        long long s = to_sample(n.start, opt.sample_rate, opt.bpm);
        long long e = to_sample(rat_add(n.start, n.dur), opt.sample_rate, opt.bpm);
        evs.push_back({s, true, n.key});
        evs.push_back({e, false, n.key});
        if (e > last_end) last_end = e;
    }
    // time order; at a tie, note-off before note-on so a re-attack retriggers cleanly
    std::stable_sort(evs.begin(), evs.end(), [](const SampleEv& a, const SampleEv& b) {
        if (a.sample != b.sample) return a.sample < b.sample;
        return (a.on ? 1 : 0) < (b.on ? 1 : 0);
    });

    // ---- sfizz synth ----
    sfizz_synth_t* synth = sfizz_create_synth();
    if (!synth) { err = "could not create the sfizz synth"; return false; }
    if (!sfizz_load_file(synth, opt.sfz_path.c_str())) {
        sfizz_free(synth);
        err = "cannot load soundfont '" + opt.sfz_path + "'";
        return false;
    }
    sfizz_set_sample_rate(synth, static_cast<float>(opt.sample_rate));
    sfizz_set_samples_per_block(synth, BLOCK);

    // ---- WAV encoder ----
    ma_encoder_config cfg =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2,
                               static_cast<ma_uint32>(opt.sample_rate));
    ma_encoder enc;
    ma_result mr = ma_encoder_init_file(path.c_str(), &cfg, &enc);
    if (mr != MA_SUCCESS) {
        sfizz_free(synth);
        err = "cannot open '" + path + "' for writing (miniaudio result "
              + std::to_string(static_cast<int>(mr)) + ")";
        return false;
    }

    // ---- render loop ----
    std::vector<float> left(BLOCK), right(BLOCK), inter(2 * BLOCK);
    float* channels[2] = {left.data(), right.data()};
    const long long cap = last_end + 10LL * opt.sample_rate; // release-tail safety net

    std::size_t ei = 0;
    bool ok = true;
    for (long long pos = 0;
         pos < cap && (pos < last_end || sfizz_get_num_active_voices(synth) > 0);
         pos += BLOCK) {
        // dispatch every event landing in [pos, pos + BLOCK)
        while (ei < evs.size() && evs[ei].sample < pos + BLOCK) {
            int delay = static_cast<int>(evs[ei].sample - pos);
            if (delay < 0) delay = 0;
            if (evs[ei].on) sfizz_send_note_on(synth, delay, evs[ei].key, opt.velocity);
            else            sfizz_send_note_off(synth, delay, evs[ei].key, 0);
            ++ei;
        }
        sfizz_render_block(synth, channels, 2, BLOCK);
        for (int i = 0; i < BLOCK; ++i) { inter[2 * i] = left[i]; inter[2 * i + 1] = right[i]; }
        ma_uint64 written = 0;
        if (ma_encoder_write_pcm_frames(&enc, inter.data(), BLOCK, &written) != MA_SUCCESS) {
            err = "failed writing PCM to '" + path + "'";
            ok = false;
            break;
        }
    }

    ma_encoder_uninit(&enc);
    sfizz_free(synth);
    return ok;
}

} // namespace calliope::backend
