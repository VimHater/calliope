#include "backend/audio.hpp"

#include "backend/score.hpp"
#include "core/instrument.hpp"

#include "miniaudio.h" // no MINIAUDIO_IMPLEMENTATION here (see miniaudio_impl.cpp)
#include "sfizz.h"
#include "tsf.h" // no TSF_IMPLEMENTATION here (see tsf_impl.cpp)

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace calliope::backend {

namespace {

constexpr int BLOCK = 512; // frames per synth render call

// sfizz preload size (floats per sample held in RAM). We render offline, so there is
// no streaming thread to keep up — every sample must be fully resident. sfizz preloads
// min(sample_length, preload_size) per sample, so the maximum means "load whole
// samples, never stream", and short samples still cost only their real size (the cap
// self-adjusts down). Memory = the instrument's actual decoded sample data.
constexpr unsigned int PRELOAD_FLOATS = 0xFFFFFFFFu; // UINT_MAX: preload entire samples

// A note-on or note-off scheduled at an absolute sample position.
struct SampleEv { long long sample; bool on; int key; int velocity; };

// Seconds (tempo already resolved by `flatten`) -> sample index, rounded to nearest.
long long to_sample(const Rational& seconds, int sample_rate) {
    long long num = seconds.num * sample_rate;
    long long den = seconds.den;
    return (num + den / 2) / den;
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}

bool is_absolute(const std::string& p) {
    return !p.empty() && (p[0] == '/' || (p.size() > 1 && p[1] == ':')); // unix / win
}

// Resolve a (possibly relative) custom .sfz path against the source file's dir.
std::string resolve(const std::string& base, const std::string& p) {
    if (p.empty() || is_absolute(p) || base.empty()) return p;
    return base + "/" + p;
}

// The SSO library root that instrument .sfz paths resolve against, and the bundled
// fallback SF2 (both compile-time; empty if not configured).
std::string sso_dir() {
#ifdef CALLIOPE_SOUNDFONT_DIR
    std::string d = CALLIOPE_SOUNDFONT_DIR;
    if (d.empty()) return d;
    return d + "/Sonatina Symphonic Orchestra/";
#else
    return std::string();
#endif
}
const char* fallback_sf2() {
#ifdef CALLIOPE_FALLBACK_SF2
    return CALLIOPE_FALLBACK_SF2;
#else
    return "";
#endif
}

// Build the sorted note-on/off events for one instrument group; fills `last_end`.
std::vector<SampleEv> events_of(const std::vector<TimedNote>& notes,
                                const AudioOptions& opt, long long& last_end) {
    std::vector<SampleEv> evs;
    evs.reserve(notes.size() * 2);
    last_end = 0;
    for (const TimedNote& n : notes) {
        long long s = to_sample(n.start, opt.sample_rate);
        long long e = to_sample(rat_add(n.start, n.dur), opt.sample_rate);
        evs.push_back({s, true, n.key, n.velocity});
        evs.push_back({e, false, n.key, n.velocity});
        if (e > last_end) last_end = e;
    }
    std::stable_sort(evs.begin(), evs.end(), [](const SampleEv& a, const SampleEv& b) {
        if (a.sample != b.sample) return a.sample < b.sample;
        return (a.on ? 1 : 0) < (b.on ? 1 : 0); // off before on at a tie
    });
    return evs;
}

// Render one group through sfizz (.sfz). Returns interleaved stereo f32, or empty
// (with `err` set) if the soundfont won't load.
std::vector<float> render_sfizz(const std::string& sfz, const std::vector<SampleEv>& evs,
                                long long last_end, const AudioOptions& opt, std::string& err) {
    sfizz_synth_t* s = sfizz_create_synth();
    if (!s) { err = "could not create the sfizz synth"; return {}; }
    if (!sfizz_load_file(s, sfz.c_str())) {
        sfizz_free(s);
        err = "cannot load soundfont '" + sfz + "'";
        return {};
    }
    sfizz_set_sample_rate(s, static_cast<float>(opt.sample_rate));
    sfizz_set_samples_per_block(s, BLOCK);
    // sfizz streams the tail of each sample from disk on a background thread, keeping
    // only `preload_size` floats (default 8192 ≈ 0.19 s) in RAM. An offline render
    // outruns that loader, so a held note falls silent once the preloaded head ends.
    // Bump the preload so whole samples sit in memory — no streaming needed offline.
    sfizz_set_preload_size(s, PRELOAD_FLOATS);

    std::vector<float> out, left(BLOCK), right(BLOCK);
    float* ch[2] = {left.data(), right.data()};
    const long long cap = last_end + 10LL * opt.sample_rate;
    std::size_t ei = 0;
    for (long long pos = 0;
         pos < cap && (pos < last_end || sfizz_get_num_active_voices(s) > 0);
         pos += BLOCK) {
        while (ei < evs.size() && evs[ei].sample < pos + BLOCK) {
            int delay = static_cast<int>(evs[ei].sample - pos);
            if (delay < 0) delay = 0;
            if (evs[ei].on) sfizz_send_note_on(s, delay, evs[ei].key, evs[ei].velocity);
            else            sfizz_send_note_off(s, delay, evs[ei].key, 0);
            ++ei;
        }
        sfizz_render_block(s, ch, 2, BLOCK);
        for (int i = 0; i < BLOCK; ++i) { out.push_back(left[i]); out.push_back(right[i]); }
    }
    sfizz_free(s);
    return out;
}

// Render one group through tsf (placeholder SF2 + GM program). tsf has no
// per-sample event delay, so onsets quantize to the block start (~12ms) — fine for
// a fallback voice. Returns interleaved stereo f32, or empty (with `err` set).
std::vector<float> render_tsf(const std::string& sf2, int gm, const std::vector<SampleEv>& evs,
                              long long last_end, const AudioOptions& opt, std::string& err) {
    tsf* f = tsf_load_filename(sf2.c_str());
    if (!f) { err = "cannot load fallback SF2 '" + sf2 + "'"; return {}; }
    tsf_set_output(f, TSF_STEREO_INTERLEAVED, opt.sample_rate, 0.0f);
    tsf_channel_set_presetnumber(f, 0, gm, 0);

    std::vector<float> out, blk(2 * BLOCK);
    const long long cap = last_end + 10LL * opt.sample_rate;
    std::size_t ei = 0;
    for (long long pos = 0;
         pos < cap && (pos < last_end || tsf_active_voice_count(f) > 0);
         pos += BLOCK) {
        while (ei < evs.size() && evs[ei].sample < pos + BLOCK) {
            if (evs[ei].on) tsf_channel_note_on(f, 0, evs[ei].key,
                                                static_cast<float>(evs[ei].velocity) / 127.0f);
            else            tsf_channel_note_off(f, 0, evs[ei].key);
            ++ei;
        }
        tsf_render_float(f, blk.data(), BLOCK, 0);
        out.insert(out.end(), blk.begin(), blk.end());
    }
    tsf_close(f);
    return out;
}

// Resolve + render one instrument group: sfizz if its .sfz exists, else the tsf
// fallback. `inst` is a named-instrument id (-1 = none/custom); a non-empty `path`
// is a user `sfz "..."`; `gmprog >= 0` is a raw `gm n` (no .sfz — straight to tsf).
// Empty result means the group was skipped (warning printed).
std::vector<float> render_group(int inst, const std::string& path, int gmprog,
                                const std::vector<SampleEv>& evs, long long last_end,
                                const AudioOptions& opt) {
    std::string sfz;
    int gm = 0;
    std::string label;
    if (gmprog >= 0) {
        gm = gmprog;                 // raw GM program: no sampler, tsf only
        label = "gm " + std::to_string(gmprog);
    } else if (!path.empty()) {
        sfz = resolve(opt.base_dir, path); // custom user soundfont
        label = "\"" + path + "\"";
    } else if (inst < 0) {
        sfz = opt.sfz_path; // un-instrumented notes: the --soundfont default voice
        label = "(default)";
    } else if (const instrument::Info* info = instrument::by_id(inst)) {
        gm = info->gm;
        std::string dir = sso_dir();
        if (!dir.empty()) sfz = dir + info->sfz_rel;
        label = info->name;
    }

    std::string err;
    if (file_exists(sfz)) {
        std::vector<float> buf = render_sfizz(sfz, evs, last_end, opt, err);
        if (!buf.empty()) return buf;
    }
    // A custom .sfz has no GM mapping; fall back to program 0 if its file is missing.
    if (file_exists(fallback_sf2())) {
        std::vector<float> buf = render_tsf(fallback_sf2(), gm, evs, last_end, opt, err);
        if (!buf.empty()) return buf;
    }

    std::fprintf(stderr, "warning: no playable soundfont for instrument %s; skipped\n",
                 label.c_str());
    return {};
}

} // namespace

bool write_wav(const music::Music& m, music::MusicId root,
               const AudioOptions& opt, const std::string& path, std::string& err) {
    std::vector<TimedNote> notes = flatten(m, root);

    // Group notes by instrument (named id or custom .sfz path), render each on its
    // own synth, and mix (sum) the groups into one master buffer. The map key keeps
    // distinct custom soundfonts (all instrument == -1) apart; std::map gives a
    // stable, deterministic order.
    std::map<std::string, std::vector<TimedNote>> groups;
    for (const TimedNote& n : notes) {
        std::string key;
        if (!n.sfz_path.empty()) key = "@" + n.sfz_path;
        else if (n.gm >= 0)      key = "%" + std::to_string(n.gm);
        else                     key = "#" + std::to_string(n.instrument);
        groups[key].push_back(n);
    }

    std::vector<float> master; // interleaved stereo f32
    for (const std::pair<const std::string, std::vector<TimedNote>>& g : groups) {
        const TimedNote& rep = g.second.front(); // all share instrument + sfz_path
        long long last_end = 0;
        std::vector<SampleEv> evs = events_of(g.second, opt, last_end);
        std::vector<float> buf = render_group(rep.instrument, rep.sfz_path, rep.gm, evs, last_end, opt);
        if (buf.size() > master.size()) master.resize(buf.size(), 0.0f);
        for (std::size_t i = 0; i < buf.size(); ++i) master[i] += buf[i];
    }

    // Summing groups (and a hot fallback synth) can exceed unity; hard-limit to
    // [-1, 1] so the f32 WAV never carries out-of-range samples that players clip.
    for (float& s : master) s = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);

    // ---- WAV encoder ----
    ma_encoder_config cfg =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2,
                               static_cast<ma_uint32>(opt.sample_rate));
    ma_encoder enc;
    ma_result mr = ma_encoder_init_file(path.c_str(), &cfg, &enc);
    if (mr != MA_SUCCESS) {
        err = "cannot open '" + path + "' for writing (miniaudio result "
              + std::to_string(static_cast<int>(mr)) + ")";
        return false;
    }
    ma_uint64 frames = master.size() / 2;
    ma_uint64 written = 0;
    bool ok = master.empty() ||
              ma_encoder_write_pcm_frames(&enc, master.data(), frames, &written) == MA_SUCCESS;
    if (!ok) err = "failed writing PCM to '" + path + "'";
    ma_encoder_uninit(&enc);
    return ok;
}

} // namespace calliope::backend
