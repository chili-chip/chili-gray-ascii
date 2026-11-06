// Sound from the 32blit synth: wind, rain and sea as filtered noise,
// small clicks for the interface, the telex, the lighthouse hum and a
// short sad tune for the title and the endings.
//
// Channels: 0 wind, 1 rain, 2 sea, 3 clicks, 4 typing, 5 hum, 6 tune.

#include <cmath>

#include "32blit.hpp"
#include "szary.hpp"

using namespace blit;

namespace sz {
namespace audio {

namespace {

bool ready = false;

void steady(int ch, uint8_t wave, uint16_t freq) {
    auto &c = channels[ch];
    c.waveforms = wave;
    c.frequency = freq;
    c.volume = 0;
    c.attack_ms = 300;
    c.decay_ms = 1;
    c.sustain = 0xffff;
    c.release_ms = 300;
    c.trigger_attack();
}

void blip(int ch, uint8_t wave, uint16_t freq, uint16_t vol, uint16_t decay) {
    auto &c = channels[ch];
    c.waveforms = wave;
    c.frequency = freq;
    c.volume = vol;
    c.attack_ms = 1;
    c.decay_ms = decay;
    c.sustain = 0;
    c.release_ms = 1;
    c.trigger_attack();
}

// the tune: a falling minor line, played slowly on a triangle
struct Note { uint16_t freq, ms; };
const Note title_tune[] = {{330, 600}, {294, 600}, {262, 900}, {0, 300}, {247, 600}, {220, 1400}};
const Note end_tune[] = {{220, 700}, {262, 700}, {247, 700}, {196, 1600}, {0, 400}, {220, 1800}};
const Note *tune = nullptr;
int tune_len = 0, tune_pos = 0;
uint32_t tune_next = 0;

float sea_phase = 0.0f;
uint32_t last_ms = 0;

}  // namespace

void init() {
    steady(0, uint8_t(Waveform::NOISE), 1800);
    steady(1, uint8_t(Waveform::NOISE), 5200);
    steady(2, uint8_t(Waveform::NOISE), 500);
    steady(5, uint8_t(Waveform::SQUARE), 55);
    ready = true;
}

void update() {
    if(!ready) return;
    uint32_t t = now();
    float dt = last_ms ? float(t - last_ms) / 1000.0f : 0.0f;
    if(dt > 0.25f) dt = 0.25f;
    last_ms = t;

    bool out = world::outdoors(st.scene);
    float w = world::wind_at(st.x);
    // indoors the storm is muffled: lower and quieter
    channels[0].frequency = out ? 1800 : 700;
    float wv = out ? 0x500 + fminf(w, 2.2f) * 0x1400 : 0x300 + fminf(w, 2.2f) * 0x300;
    channels[0].volume = uint16_t(wv);

    bool rain = st.chapter == 3 || st.chapter == 4;
    channels[1].volume = uint16_t(rain ? (out ? 0x1400 : 0x500) : 0);

    sea_phase += dt * 0.7f;
    float swell = 0.5f + 0.5f * sinf(sea_phase);
    bool sea = st.scene == S_DUNES || st.scene == S_PIER;
    channels[2].volume = uint16_t(sea ? 0x300 + swell * swell * 0x1200 : 0);

    channels[5].volume = uint16_t(world::beam > 0.0f && st.scene == S_DUNES ? 0x900 : (world::beam > 0.0f && st.scene == S_LIGHTHOUSE ? 0x1400 : 0));

    if(tune && t >= tune_next) {
        if(tune_pos >= tune_len) tune = nullptr;
        else {
            const Note &n = tune[tune_pos++];
            if(n.freq) {
                auto &c = channels[6];
                c.waveforms = uint8_t(Waveform::TRIANGLE);
                c.frequency = n.freq;
                c.volume = 0x2600;
                c.attack_ms = 40;
                c.decay_ms = uint16_t(n.ms);
                c.sustain = 0x4000;
                c.release_ms = 400;
                c.trigger_attack();
            }
            tune_next = t + n.ms;
        }
    }
}

void click() { blip(3, uint8_t(Waveform::SQUARE), 1400, 0x1800, 20); }
void type_blip() { blip(4, uint8_t(Waveform::TRIANGLE), uint16_t(520 + (now() * 7) % 120), 0x1200, 16); }
void telex() { blip(4, uint8_t(Waveform::NOISE), 3000, 0x1c00, 12); }
void page() { blip(3, uint8_t(Waveform::NOISE), 2600, 0x1400, 60); }
void thud() { blip(3, uint8_t(Waveform::TRIANGLE), 90, 0x3000, 220); }
void step() { blip(3, uint8_t(Waveform::NOISE), 700, 0x0600, 25); }
void hum(bool on) { channels[5].volume = on ? 0x1000 : 0; }

void motif(int which) {
    tune = which == 0 ? title_tune : end_tune;
    tune_len = which == 0 ? int(sizeof(title_tune) / sizeof(Note)) : int(sizeof(end_tune) / sizeof(Note));
    tune_pos = 0;
    tune_next = 0;
}

}  // namespace audio
}  // namespace sz
