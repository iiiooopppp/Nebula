#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <vector>

static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
static constexpr float kPi    = juce::MathConstants<float>::pi;

static void nlog (const juce::String& m)
{
    juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NebulaLog.txt").appendText (m + "\n");
}

static inline float smooth01 (float x)
{
    x = juce::jlimit (0.f, 1.f, x);
    return x * x * (3.f - 2.f * x);
}

//==============================================================================
// Параметры
//==============================================================================
struct ParamDef { const char* id; const char* name; float lo, hi, def, skew, step; bool choice; };

static const ParamDef kParams[] =
{
    { "wave1",   "Osc A",         0.f,    3.f,     0.f,    1.f,   1.f, true  },
    { "wave2",   "Osc B",         0.f,    3.f,     0.f,    1.f,   1.f, true  },
    { "mixb",    "Osc B Level",   0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "detune",  "Detune",        0.f,    1.f,     0.15f,  1.f,   0.f, false },
    { "fmamt",   "FM",            0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "fmratio", "FM Ratio",      0.5f,   8.f,     2.f,    1.f,   0.f, false },
    { "sub",     "Sub",           0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "noise",   "Noise",         0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "cutoff",  "Cutoff",        30.f,   18000.f, 8000.f, 0.3f,  0.f, false },
    { "res",     "Resonance",     0.f,    1.f,     0.1f,   1.f,   0.f, false },
    { "fenv",    "Filter Env",    0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "fdecay",  "Filter Decay",  0.02f,  3.f,     0.3f,   0.5f,  0.f, false },
    { "attack",  "Attack",        0.001f, 4.f,     0.005f, 0.35f, 0.f, false },
    { "decay",   "Decay",         0.02f,  5.f,     0.8f,   0.4f,  0.f, false },
    { "sustain", "Sustain",       0.f,    1.f,     0.f,    1.f,   0.f, false },
    { "release", "Release",       0.01f,  6.f,     0.4f,   0.4f,  0.f, false },
    { "oct",     "Octave",       -2.f,    2.f,     0.f,    1.f,   1.f, false },
    { "vol",     "Volume",        0.f,    1.f,     0.7f,   1.f,   0.f, false },
    { "dmix",    "Echo",          0.f,    1.f,     0.2f,   1.f,   0.f, false },
    { "dtime",   "Echo Time",     0.05f,  0.8f,    0.35f,  1.f,   0.f, false },
    { "dfb",     "Echo Feedback", 0.f,    0.9f,    0.35f,  1.f,   0.f, false },
    { "rmix",    "Space",         0.f,    1.f,     0.25f,  1.f,   0.f, false },
    { "rsize",   "Space Size",    0.f,    1.f,     0.6f,   1.f,   0.f, false },
};

enum ParamIndex
{
    WAVE1, WAVE2, MIXB, DETUNE, FMAMT, FMRATIO, SUB, NOISE, CUTOFF, RES, FENV, FDECAY,
    ATTACK, DECAY, SUSTAIN, RELEASE, OCT, VOL, DMIX, DTIME, DFB, RMIX, RSIZE, NUM_PARAMS
};

struct VoiceParams
{
    int   wave1 = 0, wave2 = 0;
    float mixb = 0.f, detune = 0.f, fmamt = 0.f, fmratio = 2.f, sub = 0.f, noise = 0.f;
    float cutoff = 8000.f, res = 0.1f, fenv = 0.f, fdecay = 0.3f;
    float attack = 0.005f, decay = 0.8f, sustain = 0.f, release = 0.4f, oct = 0.f;
    float dtime = 0.35f, dfb = 0.35f, dmix = 0.2f, rmix = 0.25f, rsize = 0.6f, vol = 0.7f;
};

//==============================================================================
// Пресеты: папки жанров
//==============================================================================
struct PV        { const char* id; float v; };
struct PresetDef { juce::String name; std::vector<PV> v; };
struct GenreDef  { juce::String name; juce::Colour a, b; std::vector<PresetDef> presets; };

static PresetDef P (const char* n, std::initializer_list<PV> l)
{
    PresetDef d;
    d.name = n;
    d.v = std::vector<PV> (l);
    return d;
}

static const std::vector<GenreDef>& genres()
{
    static const std::vector<GenreDef> g =
    {
        { "Futuristic Glo", juce::Colour (0xff5ee7ff), juce::Colour (0xffff6ad5), {
            P ("Glass Bell",       { {"fmamt",.55f}, {"fmratio",3.5f}, {"cutoff",12000.f}, {"fdecay",.8f}, {"decay",1.8f}, {"release",1.2f}, {"oct",1.f}, {"dmix",.25f}, {"rmix",.35f} }),
            P ("Holo Strings",     { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.8f}, {"detune",.35f}, {"cutoff",3500.f}, {"res",.12f}, {"fenv",.2f}, {"fdecay",1.2f}, {"attack",.35f}, {"decay",1.f}, {"sustain",.8f}, {"release",1.2f}, {"rmix",.4f}, {"dmix",.1f} }),
            P ("Neon Pluck",       { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.3f}, {"detune",.15f}, {"cutoff",1200.f}, {"fenv",.7f}, {"fdecay",.22f}, {"decay",.45f}, {"release",.3f}, {"dmix",.3f}, {"dtime",.3f}, {"dfb",.4f} }),
            P ("Zero Gravity Pad", { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.6f}, {"detune",.5f}, {"cutoff",2200.f}, {"attack",1.2f}, {"decay",1.5f}, {"sustain",.9f}, {"release",2.5f}, {"rmix",.55f}, {"rsize",.9f} }),
            P ("Crystal Hit",      { {"fmamt",.8f}, {"fmratio",5.f}, {"noise",.12f}, {"cutoff",9000.f}, {"fdecay",.12f}, {"decay",.35f}, {"release",.4f}, {"oct",1.f}, {"rmix",.3f} }),
            P ("Starlight Keys",   { {"wave1",1.f}, {"fmamt",.3f}, {"fmratio",2.f}, {"mixb",.3f}, {"cutoff",5000.f}, {"fenv",.3f}, {"fdecay",.5f}, {"decay",1.2f}, {"sustain",.1f}, {"release",.8f}, {"dmix",.2f}, {"rmix",.3f} }),
            P ("Comet Lead",       { {"wave1",3.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.2f}, {"cutoff",4500.f}, {"res",.25f}, {"fenv",.4f}, {"fdecay",.4f}, {"decay",.3f}, {"sustain",.7f}, {"release",.4f}, {"dmix",.35f}, {"dfb",.45f}, {"rmix",.3f} }),
        } },

        { "Plugg", juce::Colour (0xffffb3e6), juce::Colour (0xff8dffd8), {
            P ("Sugar Bell",    { {"fmamt",.35f}, {"fmratio",2.f}, {"cutoff",8000.f}, {"fdecay",.4f}, {"decay",1.f}, {"release",.7f}, {"oct",1.f}, {"dmix",.3f}, {"dtime",.375f}, {"dfb",.45f}, {"rmix",.3f} }),
            P ("Cloud Harp",    { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.5f}, {"detune",.1f}, {"fmamt",.2f}, {"fmratio",4.f}, {"cutoff",6000.f}, {"fenv",.4f}, {"fdecay",.3f}, {"decay",.9f}, {"release",.6f}, {"dmix",.3f}, {"rmix",.3f} }),
            P ("Soft Mallet",   { {"fmamt",.5f}, {"fmratio",3.f}, {"cutoff",4500.f}, {"fdecay",.15f}, {"decay",.5f}, {"release",.3f}, {"noise",.05f}, {"dmix",.25f}, {"rmix",.25f} }),
            P ("Pastel Keys",   { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.25f}, {"cutoff",2800.f}, {"fenv",.5f}, {"fdecay",.25f}, {"decay",.7f}, {"release",.4f}, {"dmix",.3f}, {"rmix",.3f} }),
            P ("Bubble Pluck",  { {"wave1",3.f}, {"cutoff",900.f}, {"res",.3f}, {"fenv",.8f}, {"fdecay",.15f}, {"decay",.3f}, {"release",.2f}, {"sub",.2f}, {"dmix",.3f} }),
            P ("Candy Strings", { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.7f}, {"detune",.3f}, {"cutoff",3000.f}, {"attack",.2f}, {"decay",1.f}, {"sustain",.7f}, {"release",.9f}, {"rmix",.4f} }),
        } },

        { "Dairy Plugg", juce::Colour (0xff9fd8ff), juce::Colour (0xffffe9b8), {
            P ("Air Bell",         { {"fmamt",.25f}, {"fmratio",4.f}, {"cutoff",11000.f}, {"fdecay",.5f}, {"decay",1.6f}, {"release",1.4f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.5f}, {"rsize",.85f} }),
            P ("Cloud Pad",        { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.6f}, {"detune",.35f}, {"cutoff",3000.f}, {"noise",.04f}, {"attack",1.2f}, {"decay",1.5f}, {"sustain",.9f}, {"release",2.5f}, {"rmix",.6f}, {"rsize",.95f} }),
            P ("Wind Chime",       { {"fmamt",.5f}, {"fmratio",7.f}, {"cutoff",14000.f}, {"fdecay",.25f}, {"decay",2.f}, {"release",1.8f}, {"oct",2.f}, {"dmix",.35f}, {"dfb",.5f}, {"rmix",.5f} }),
            P ("Breeze Pluck",     { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.2f}, {"cutoff",3500.f}, {"fenv",.5f}, {"fdecay",.3f}, {"decay",.8f}, {"release",.5f}, {"noise",.03f}, {"dmix",.3f}, {"rmix",.4f} }),
            P ("Feather Keys",     { {"wave2",1.f}, {"mixb",.4f}, {"fmamt",.15f}, {"fmratio",2.f}, {"cutoff",7000.f}, {"fenv",.3f}, {"fdecay",.5f}, {"decay",1.4f}, {"sustain",.05f}, {"release",1.1f}, {"rmix",.5f} }),
            P ("Sky Harp",         { {"wave1",1.f}, {"fmamt",.3f}, {"fmratio",3.f}, {"cutoff",8000.f}, {"fdecay",.35f}, {"decay",1.3f}, {"release",.9f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.45f} }),
            P ("Daydream Strings", { {"wave1",2.f}, {"wave2",1.f}, {"mixb",.6f}, {"detune",.3f}, {"cutoff",2600.f}, {"attack",.6f}, {"decay",1.2f}, {"sustain",.8f}, {"release",1.8f}, {"rmix",.5f} }),
        } },

        { "Dark Plugg", juce::Colour (0xff9b5cff), juce::Colour (0xffff3a5e), {
            P ("Graveyard Bell", { {"fmamt",.6f}, {"fmratio",3.5f}, {"cutoff",4000.f}, {"fdecay",.6f}, {"decay",2.2f}, {"release",1.5f}, {"rmix",.55f}, {"rsize",.9f}, {"dmix",.25f} }),
            P ("Haunted Choir",  { {"wave1",1.f}, {"wave2",1.f}, {"mixb",.7f}, {"detune",.45f}, {"cutoff",1800.f}, {"res",.15f}, {"attack",.8f}, {"decay",1.f}, {"sustain",.9f}, {"release",2.f}, {"rmix",.6f}, {"rsize",.95f} }),
            P ("Cold Pluck",     { {"wave1",2.f}, {"cutoff",700.f}, {"res",.25f}, {"fenv",.8f}, {"fdecay",.2f}, {"decay",.5f}, {"release",.3f}, {"sub",.3f}, {"dmix",.3f}, {"rmix",.35f} }),
            P ("Shadow Strings", { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.8f}, {"detune",.4f}, {"cutoff",1400.f}, {"attack",.5f}, {"decay",1.2f}, {"sustain",.8f}, {"release",1.6f}, {"rmix",.45f} }),
            P ("Void Hit",       { {"fmamt",.7f}, {"fmratio",1.5f}, {"noise",.25f}, {"sub",.6f}, {"cutoff",2500.f}, {"fenv",.6f}, {"fdecay",.1f}, {"decay",.4f}, {"release",.5f}, {"oct",-1.f}, {"rmix",.5f} }),
            P ("Crimson Keys",   { {"wave1",1.f}, {"fmamt",.25f}, {"fmratio",2.f}, {"cutoff",2000.f}, {"fenv",.4f}, {"fdecay",.4f}, {"decay",.9f}, {"release",.6f}, {"rmix",.4f} }),
        } },

        { "Pluggnb", juce::Colour (0xffff9b5e), juce::Colour (0xffff5e9b), {
            P ("Silk Keys",      { {"fmamt",.3f}, {"fmratio",1.f}, {"wave2",1.f}, {"mixb",.3f}, {"cutoff",6000.f}, {"fenv",.25f}, {"fdecay",.6f}, {"decay",1.6f}, {"sustain",.1f}, {"release",.9f}, {"rmix",.35f} }),
            P ("Love Rhodes",    { {"fmamt",.45f}, {"fmratio",1.f}, {"sub",.15f}, {"cutoff",4500.f}, {"fdecay",.5f}, {"decay",1.8f}, {"sustain",.15f}, {"release",1.f}, {"dmix",.2f}, {"rmix",.3f} }),
            P ("Velvet Pad",     { {"wave1",1.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.3f}, {"cutoff",2500.f}, {"attack",1.f}, {"decay",1.5f}, {"sustain",.85f}, {"release",2.2f}, {"rmix",.5f} }),
            P ("Melt Bell",      { {"fmamt",.4f}, {"fmratio",2.f}, {"cutoff",7000.f}, {"fdecay",.5f}, {"decay",1.4f}, {"release",1.1f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.4f} }),
            P ("Sunset Strings", { {"wave1",2.f}, {"wave2",1.f}, {"mixb",.6f}, {"detune",.3f}, {"cutoff",3200.f}, {"attack",.3f}, {"decay",1.f}, {"sustain",.8f}, {"release",1.3f}, {"rmix",.4f} }),
            P ("Heartbeat Pluck",{ {"wave1",1.f}, {"wave2",3.f}, {"mixb",.2f}, {"cutoff",1800.f}, {"fenv",.6f}, {"fdecay",.25f}, {"decay",.6f}, {"release",.35f}, {"sub",.25f}, {"dmix",.25f} }),
        } },

        { "Rage", juce::Colour (0xffff3b3b), juce::Colour (0xffffd23b), {
            P ("Rage Stab",     { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.9f}, {"detune",.4f}, {"cutoff",3500.f}, {"res",.2f}, {"fenv",.6f}, {"fdecay",.18f}, {"decay",.35f}, {"sustain",.3f}, {"release",.15f}, {"dmix",.15f}, {"rmix",.15f} }),
            P ("Super Lead",    { {"wave1",2.f}, {"wave2",2.f}, {"mixb",1.f}, {"detune",.6f}, {"cutoff",6000.f}, {"res",.15f}, {"decay",.3f}, {"sustain",.85f}, {"release",.3f}, {"dmix",.2f} }),
            P ("Siren Bell",    { {"wave1",3.f}, {"fmamt",.6f}, {"fmratio",7.f}, {"cutoff",8000.f}, {"fdecay",.3f}, {"decay",.7f}, {"release",.3f}, {"oct",1.f}, {"dmix",.2f} }),
            P ("Opium Strings", { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.6f}, {"detune",.35f}, {"cutoff",2800.f}, {"res",.2f}, {"attack",.08f}, {"decay",.6f}, {"sustain",.6f}, {"release",.5f}, {"rmix",.25f} }),
            P ("Brick Hit",     { {"wave1",3.f}, {"noise",.35f}, {"sub",.5f}, {"cutoff",2500.f}, {"fenv",.7f}, {"fdecay",.1f}, {"decay",.25f}, {"release",.2f}, {"oct",-1.f}, {"dmix",.1f} }),
            P ("Demon Pluck",   { {"wave1",2.f}, {"wave2",2.f}, {"mixb",.5f}, {"detune",.5f}, {"cutoff",900.f}, {"res",.35f}, {"fenv",.9f}, {"fdecay",.2f}, {"decay",.4f}, {"release",.2f}, {"sub",.3f}, {"dmix",.2f} }),
        } },

        { "Hyperpop", juce::Colour (0xff6bff9e), juce::Colour (0xffff4fd8), {
            P ("Glitter Lead",     { {"wave1",2.f}, {"wave2",3.f}, {"mixb",.5f}, {"detune",.25f}, {"fmamt",.2f}, {"fmratio",2.f}, {"cutoff",9000.f}, {"res",.2f}, {"decay",.2f}, {"sustain",.8f}, {"release",.2f}, {"dmix",.3f}, {"dtime",.22f}, {"dfb",.5f} }),
            P ("Sugar Rush Pluck", { {"wave1",3.f}, {"cutoff",1500.f}, {"res",.35f}, {"fenv",.9f}, {"fdecay",.12f}, {"decay",.3f}, {"release",.15f}, {"oct",1.f}, {"dmix",.35f} }),
            P ("Chrome Saw",       { {"wave1",2.f}, {"wave2",2.f}, {"mixb",1.f}, {"detune",.7f}, {"cutoff",7000.f}, {"res",.1f}, {"attack",.01f}, {"decay",.5f}, {"sustain",.8f}, {"release",.4f}, {"rmix",.25f} }),
            P ("Bubblegum Bell",   { {"fmamt",.9f}, {"fmratio",4.f}, {"cutoff",12000.f}, {"fdecay",.2f}, {"decay",1.f}, {"release",.5f}, {"oct",1.f}, {"dmix",.25f} }),
            P ("Laser Hit",        { {"fmamt",1.f}, {"fmratio",6.f}, {"noise",.2f}, {"cutoff",10000.f}, {"fdecay",.08f}, {"decay",.3f}, {"release",.2f}, {"dmix",.2f} }),
            P ("Pixel Keys",       { {"wave1",3.f}, {"wave2",1.f}, {"mixb",.4f}, {"cutoff",3500.f}, {"fenv",.4f}, {"fdecay",.2f}, {"decay",.5f}, {"sustain",.2f}, {"release",.25f}, {"dmix",.25f} }),
        } },

        { "Dreamcore", juce::Colour (0xffc9a8ff), juce::Colour (0xff8fd0ff), {
            P ("Music Box",      { {"fmamt",.25f}, {"fmratio",5.f}, {"cutoff",9000.f}, {"fdecay",.3f}, {"decay",1.4f}, {"release",1.f}, {"oct",1.f}, {"dmix",.3f}, {"rmix",.45f} }),
            P ("Faded Pad",      { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.6f}, {"detune",.4f}, {"cutoff",1800.f}, {"attack",1.5f}, {"decay",1.5f}, {"sustain",.9f}, {"release",3.f}, {"rmix",.65f}, {"rsize",.95f} }),
            P ("Lullaby Bell",   { {"fmamt",.4f}, {"fmratio",3.f}, {"cutoff",6000.f}, {"fdecay",.7f}, {"decay",2.4f}, {"release",2.f}, {"oct",1.f}, {"rmix",.55f}, {"dmix",.3f}, {"dfb",.5f} }),
            P ("Childhood Keys", { {"wave1",1.f}, {"wave2",0.f}, {"mixb",.4f}, {"cutoff",3500.f}, {"fenv",.3f}, {"fdecay",.5f}, {"decay",1.2f}, {"sustain",.1f}, {"release",.9f}, {"rmix",.4f} }),
            P ("Ghost Strings",  { {"wave1",2.f}, {"wave2",1.f}, {"mixb",.6f}, {"detune",.5f}, {"cutoff",1500.f}, {"attack",.9f}, {"decay",1.f}, {"sustain",.8f}, {"release",2.2f}, {"rmix",.6f} }),
            P ("Memory Hit",     { {"fmamt",.5f}, {"fmratio",2.5f}, {"noise",.1f}, {"cutoff",4000.f}, {"fdecay",.2f}, {"decay",.8f}, {"release",.8f}, {"rmix",.5f} }),
        } },
    };
    return g;
}

//==============================================================================
// Голос синтезатора
//==============================================================================
static inline float polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;         return t + t - t * t - 1.f; }
    if (t > 1.f - dt)   { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

static inline float oscShape (int wave, float t, float dt)
{
    switch (wave)
    {
        case 0:  return std::sin (kTwoPi * t);
        case 1:  return t < 0.25f ? 4.f * t : (t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f);
        case 2:  return 2.f * t - 1.f - polyBlep (t, dt);
        default:
        {
            float v = t < 0.5f ? 1.f : -1.f;
            v += polyBlep (t, dt);
            float t2 = t + 0.5f;
            if (t2 >= 1.f) t2 -= 1.f;
            v -= polyBlep (t2, dt);
            return v;
        }
    }
}

struct NebulaSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class NebulaVoice : public juce::SynthesiserVoice
{
public:
    explicit NebulaVoice (const VoiceParams& p) : vp (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<NebulaSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        sr = (float) getSampleRate();
        if (sr < 8000.f) sr = 44100.f;
        const float note = (float) midiNote + 12.f * vp.oct;
        freq = 440.f * std::pow (2.f, (note - 69.f) / 12.f);
        vel = velocity;
        phA = 0.f;
        phB = rng.nextFloat();
        phS = 0.f;
        phM = 0.f;
        detuneRatio = std::pow (2.f, vp.detune * 50.f / 1200.f);
        env = 1.f;
        envCoef = std::exp (-1.f / (juce::jmax (0.005f, vp.fdecay) * sr));
        z1 = z2 = 0.f;
        pan = (rng.nextFloat() - 0.5f) * 0.6f;

        adsr.setSampleRate ((double) sr);
        adsr.setParameters (juce::ADSR::Parameters (vp.attack, vp.decay, vp.sustain, vp.release));
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.setParameters (juce::ADSR::Parameters (vp.attack, vp.decay, vp.sustain, vp.release));
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        float* L = out.getWritePointer (0, startSample);
        float* R = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

        const float dtA = freq / sr;
        const float dtB = dtA * detuneRatio;
        const float gL  = std::cos ((pan + 1.f) * kPi * 0.25f);
        const float gR  = std::sin ((pan + 1.f) * kPi * 0.25f);

        for (int i = 0; i < numSamples; ++i)
        {
            const float a = adsr.getNextSample();
            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            env *= envCoef;

            // FM-модулятор
            float mod = 0.f;
            if (vp.fmamt > 0.001f)
            {
                phM += dtA * vp.fmratio;
                phM -= std::floor (phM);
                mod = std::sin (kTwoPi * phM) * vp.fmamt * 7.f * (0.25f + 0.75f * env);
            }

            float pa = phA + mod / kTwoPi;
            pa -= std::floor (pa);

            float s = oscShape (vp.wave1, pa, dtA);
            if (vp.mixb > 0.001f) s += vp.mixb * oscShape (vp.wave2, phB, dtB);
            if (vp.sub > 0.001f)  s += vp.sub * std::sin (kTwoPi * phS);
            if (vp.noise > 0.001f) s += vp.noise * env * (rng.nextFloat() * 2.f - 1.f);

            phA += dtA; if (phA >= 1.f) phA -= 1.f;
            phB += dtB; if (phB >= 1.f) phB -= 1.f;
            phS += dtA * 0.5f; if (phS >= 1.f) phS -= 1.f;

            // фильтр (TPT state variable, low-pass)
            float fc = vp.cutoff * std::pow (2.f, vp.fenv * 5.f * env);
            fc = juce::jlimit (20.f, 0.45f * sr, fc);
            const float g  = std::tan (kPi * fc / sr);
            const float Rr = 1.f / (2.f * (0.55f + vp.res * 9.f));
            const float h  = (s - (2.f * Rr + g) * z1 - z2) / (1.f + 2.f * Rr * g + g * g);
            const float bp = g * h + z1;
            z1 = g * h + bp;
            const float lp = g * bp + z2;
            z2 = g * bp + lp;

            float o = lp * a * vel * 0.5f;
            if (! std::isfinite (o)) { o = 0.f; z1 = z2 = 0.f; }
            L[i] += o * gL;
            if (R != nullptr) R[i] += o * gR;
        }
    }

private:
    const VoiceParams& vp;
    juce::ADSR adsr;
    juce::Random rng;
    float sr = 44100.f, freq = 440.f, vel = 1.f, pan = 0.f, detuneRatio = 1.f;
    float phA = 0.f, phB = 0.f, phS = 0.f, phM = 0.f;
    float env = 1.f, envCoef = 0.999f;
    float z1 = 0.f, z2 = 0.f;
};

//==============================================================================
// Процессор
//==============================================================================
class NebulaProcessor : public juce::AudioProcessor
{
public:
    NebulaProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", createLayout())
    {
        nlog ("processor ctor start");
        for (int i = 0; i < NUM_PARAMS; ++i)
            raw[i] = apvts.getRawParameterValue (kParams[i].id);

        for (auto& b : noteState)
            b.store (false);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new NebulaVoice (vp));
        synth.addSound (new NebulaSound());

        applyPreset (0, 0, true);
        nlog ("processor ctor end");
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square" };

        for (auto& d : kParams)
        {
            if (d.choice)
                layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { d.id, 1 }, d.name, waves, (int) d.def));
            else
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { d.id, 1 }, d.name,
                    juce::NormalisableRange<float> (d.lo, d.hi, d.step, d.skew), d.def));
        }
        return layout;
    }

    const juce::String getName() const override { return "Nebula"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        nlog ("prepareToPlay");
        sr = sampleRate;
        synth.setCurrentPlaybackSampleRate (sampleRate);
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        dSize = (int) (sampleRate * 1.0) + 8;
        for (auto& b : dBuf)
            b.assign ((size_t) dSize, 0.f);
        dW = 0;
    }
    void releaseResources() override {}

    void updateParams()
    {
        auto v = [this] (int i) { return raw[i]->load(); };
        vp.wave1   = (int) std::lround (v (WAVE1));
        vp.wave2   = (int) std::lround (v (WAVE2));
        vp.mixb    = v (MIXB);
        vp.detune  = v (DETUNE);
        vp.fmamt   = v (FMAMT);
        vp.fmratio = v (FMRATIO);
        vp.sub     = v (SUB);
        vp.noise   = v (NOISE);
        vp.cutoff  = v (CUTOFF);
        vp.res     = v (RES);
        vp.fenv    = v (FENV);
        vp.fdecay  = v (FDECAY);
        vp.attack  = v (ATTACK);
        vp.decay   = v (DECAY);
        vp.sustain = v (SUSTAIN);
        vp.release = v (RELEASE);
        vp.oct     = std::round (v (OCT));
        vp.vol     = v (VOL);
        vp.dmix    = v (DMIX);
        vp.dtime   = v (DTIME);
        vp.dfb     = v (DFB);
        vp.rmix    = v (RMIX);
        vp.rsize   = v (RSIZE);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        static bool firstBlock = true;
        if (firstBlock) { firstBlock = false; nlog ("processBlock first"); }

        const int n = buffer.getNumSamples();
        buffer.clear();
        if (buffer.getNumChannels() < 2)
            return;

        updateParams();
        keyboardState.processNextMidiBuffer (midi, 0, n, true);

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                noteState[(size_t) msg.getNoteNumber()].store (true);
            else if (msg.isNoteOff())
                noteState[(size_t) msg.getNoteNumber()].store (false);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                for (auto& b : noteState) b.store (false);
        }

        synth.renderNextBlock (buffer, midi, 0, n);

        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        // эхо (пинг-понг)
        if (dSize > 16)
        {
            const int ds = juce::jlimit (1, dSize - 1, (int) (vp.dtime * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                int r = dW - ds;
                if (r < 0) r += dSize;
                const float dl = dBuf[0][(size_t) r];
                const float dr = dBuf[1][(size_t) r];
                const float in = (L[i] + R[i]) * 0.5f;

                dBuf[0][(size_t) dW] = in + dr * vp.dfb;
                dBuf[1][(size_t) dW] = dl * vp.dfb;

                L[i] += dl * vp.dmix;
                R[i] += dr * vp.dmix;

                if (++dW >= dSize) dW = 0;
            }
        }

        // реверб
        juce::Reverb::Parameters rp;
        rp.roomSize   = vp.rsize;
        rp.damping    = 0.45f;
        rp.wetLevel   = vp.rmix * 0.4f;
        rp.dryLevel   = 1.f - vp.rmix * 0.2f;
        rp.width      = 1.f;
        rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        reverb.processStereo (L, R, n);

        // мастер
        float peak = 0.f;
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (L[i])) L[i] = 0.f;
            if (! std::isfinite (R[i])) R[i] = 0.f;
            L[i] = std::tanh (L[i] * 1.1f) * vp.vol;
            R[i] = std::tanh (R[i] * 1.1f) * vp.vol;
            peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
        }
        level.store (peak);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    void setParamValue (const char* id, float v, bool silent = false)
    {
        if (auto* p = apvts.getParameter (id))
        {
            if (silent)
            {
                p->setValue (p->convertTo0to1 (v));
            }
            else
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                p->endChangeGesture();
            }
        }
    }

    void applyPreset (int g, int p, bool silent = false)
    {
        const auto& gs = genres();
        if (g < 0 || g >= (int) gs.size()) return;
        if (p < 0 || p >= (int) gs[(size_t) g].presets.size()) return;

        for (auto& d : kParams)
            setParamValue (d.id, d.def, silent);
        for (auto& pv : gs[(size_t) g].presets[(size_t) p].v)
            setParamValue (pv.id, pv.v, silent);

        apvts.state.setProperty ("genre", g, nullptr);
        apvts.state.setProperty ("preset", p, nullptr);
    }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<bool>, 128> noteState;
    std::atomic<float> level { 0.f };

private:
    double sr = 44100.0;
    VoiceParams vp;
    juce::Synthesiser synth;
    juce::Reverb reverb;
    std::atomic<float>* raw[NUM_PARAMS] {};
    std::vector<float> dBuf[2];
    int dSize = 0, dW = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaProcessor)
};

//==============================================================================
// Вид: ручки и списки
//==============================================================================
class NebulaLook : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xff5ee7ff };

    NebulaLook()
    {
        setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.35f));
        setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.3f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startA, float endA, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
        auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;

        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f * (0.3f + pos)), c.x, c.y,
                                                 accent.withAlpha (0.f), c.x, c.y - r * 1.8f, true));
        g.fillEllipse (c.x - r * 1.8f, c.y - r * 1.8f, r * 3.6f, r * 3.6f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f44), c.x, c.y - r,
                                                 juce::Colour (0xff0c0d18), c.x, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 1.2f);

        const float ar = r * 0.8f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, endA, true);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (track, juce::PathStrokeType (3.f));

        const float a = startA + pos * (endA - startA);
        juce::Path val;
        val.addCentredArc (c.x, c.y, ar, ar, 0.f, startA, a, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colours::white);
        g.drawLine (c.x + std::sin (a) * r * 0.2f, c.y - std::cos (a) * r * 0.2f,
                    c.x + std::sin (a) * r * 0.58f, c.y - std::cos (a) * r * 0.58f, 2.5f);
    }
};

struct ListModel : public juce::ListBoxModel
{
    std::function<int()> count;
    std::function<juce::String (int)> text;
    std::function<void (int)> select;
    std::function<juce::Colour()> accent;

    int getNumRows() override { return count ? count() : 0; }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
        {
            g.setColour ((accent ? accent() : juce::Colours::white).withAlpha (0.38f));
            g.fillRoundedRectangle (2.f, 1.f, (float) w - 4.f, (float) h - 2.f, 7.f);
        }
        g.setColour (juce::Colours::white.withAlpha (selected ? 1.f : 0.78f));
        g.setFont (juce::Font (14.5f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (text ? text (row) : juce::String(), 12, 0, w - 16, h, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (select) select (row);
    }
};

//==============================================================================
// Помощники для рисования сцен
//==============================================================================
static float hash1 (int n)
{
    const float h = std::sin ((float) n * 12.9898f) * 43758.5453f;
    return h - std::floor (h);
}

static void puff (juce::Graphics& g, float x, float y, float s, juce::Colour c, float alpha)
{
    for (int k = 0; k < 5; ++k)
    {
        const float px = x + ((float) k - 2.f) * 34.f * s;
        const float py = y + ((k % 2) ? -9.f : 5.f) * s;
        const float pr = (40.f + 9.f * (float) (k % 3)) * s;
        g.setGradientFill (juce::ColourGradient (c.withAlpha (alpha), px, py, c.withAlpha (0.f), px + pr, py, true));
        g.fillEllipse (px - pr, py - pr, pr * 2.f, pr * 2.f);
    }
}

static void sparkle (juce::Graphics& g, float x, float y, float r, juce::Colour c)
{
    juce::Path p;
    p.startNewSubPath (x, y - r);
    p.quadraticTo (x, y, x + r, y);
    p.quadraticTo (x, y, x, y + r);
    p.quadraticTo (x, y, x - r, y);
    p.quadraticTo (x, y, x, y - r);
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

static juce::Path heartPath (float cx, float cy, float s)
{
    juce::Path p;
    p.startNewSubPath (cx, cy + s);
    p.cubicTo (cx - 1.6f * s, cy - 0.2f * s, cx - 0.5f * s, cy - 1.1f * s, cx, cy - 0.35f * s);
    p.cubicTo (cx + 0.5f * s, cy - 1.1f * s, cx + 1.6f * s, cy - 0.2f * s, cx, cy + s);
    p.closeSubPath();
    return p;
}

static void hillLayer (juce::Graphics& g, float W, float H, float baseY, float amp, float f, float ph, juce::Colour c)
{
    juce::Path hill;
    hill.startNewSubPath (0.f, H);
    for (float x = 0.f; x <= W + 20.f; x += 20.f)
        hill.lineTo (x, baseY - amp * (0.5f + 0.5f * std::sin (x * f + ph)) - amp * 0.3f * std::sin (x * f * 2.3f + ph * 1.7f));
    hill.lineTo (W + 20.f, H);
    hill.closeSubPath();
    g.setColour (c);
    g.fillPath (hill);
}

static void branch (juce::Graphics& g, float x, float y, float ang, float len, int depth, float thick)
{
    if (depth <= 0 || len < 4.f) return;
    const float x2 = x + std::sin (ang) * len;
    const float y2 = y - std::cos (ang) * len;
    g.drawLine (x, y, x2, y2, thick);
    branch (g, x2, y2, ang - 0.5f, len * 0.72f, depth - 1, thick * 0.7f);
    branch (g, x2, y2, ang + 0.45f, len * 0.7f, depth - 1, thick * 0.7f);
}

static void drawBody (juce::Graphics& g, float x, float yN, float yD, float r, juce::Colour cN, juce::Colour cD, float d)
{
    const float y = yN + (yD - yN) * d;
    const juce::Colour c = cN.interpolatedWith (cD, d);
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.5f), x, y, c.withAlpha (0.f), x + r * 3.2f, y, true));
    g.fillEllipse (x - r * 3.2f, y - r * 3.2f, r * 6.4f, r * 6.4f);
    g.setColour (c);
    g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f);
}

//==============================================================================
// Окно
//==============================================================================
class NebulaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NebulaEditor (NebulaProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        nlog ("editor: start");
        setSize ((int) kW, (int) kH);
        nlog ("editor: setSize done");
        setLookAndFeel (&look);
        nlog ("editor: LAF done");

        // списки
        genreModel.count  = [] { return (int) genres().size(); };
        genreModel.text   = [] (int i) { return genres()[(size_t) i].name; };
        genreModel.accent = [this] { return curA; };
        genreModel.select = [this] (int row) { curGenre = row; refreshLists(); };

        presetModel.count  = [this] { return (int) genres()[(size_t) curGenre].presets.size(); };
        presetModel.text   = [this] (int i) { return genres()[(size_t) curGenre].presets[(size_t) i].name; };
        presetModel.accent = [this] { return curA; };
        presetModel.select = [this] (int row) { loadPreset (curGenre, row); };

        for (auto* l : { &genreList, &presetList })
        {
            l->setRowHeight (27);
            l->setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            l->setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (*l);
        }
        genreList.setModel (&genreModel);
        presetList.setModel (&presetModel);

        nlog ("editor: lists done");
        // кнопки
        prevBtn.onClick = [this] { step (-1); };
        nextBtn.onClick = [this] { step (1); };
        skyBtn.onClick  = [this]
        {
            skyMode = (skyMode + 1) % 3;
            skyBtn.setButtonText (skyMode == 0 ? "SKY: AUTO" : (skyMode == 1 ? "SKY: NIGHT" : "SKY: DAY"));
        };
        addAndMakeVisible (prevBtn);
        addAndMakeVisible (nextBtn);
        addAndMakeVisible (skyBtn);

        nlog ("editor: buttons done");
        // ручки
        struct KD { const char* id; const char* label; };
        static const KD kds[12] = {
            { "cutoff", "CUTOFF" }, { "res", "RESO" },   { "fenv", "FILTER ENV" },
            { "attack", "ATTACK" }, { "release", "RELEASE" }, { "detune", "DETUNE" },
            { "fmamt", "FM" },      { "sub", "SUB" },    { "noise", "NOISE" },
            { "dmix", "ECHO" },     { "rmix", "SPACE" }, { "vol", "VOLUME" } };

        for (auto& k : kds)
        {
            auto kn = std::make_unique<Knob>();
            kn->label = k.label;
            kn->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            kn->s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            kn->s.setRotaryParameters (kPi * 1.2f, kPi * 2.8f, true);
            kn->s.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (kn->s);
            if (proc.apvts.getParameter (k.id) != nullptr)
                kn->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, k.id, kn->s);
            else
                nlog (juce::String ("missing parameter ") + k.id);
            knobs.push_back (std::move (kn));
        }

        nlog ("editor: knobs done");
        // клавиатура
        keyboard.setAvailableRange (36, 96);
        keyboard.setKeyWidth (27.f);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff2f0ff));
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff14141f));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0x33000000));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour (0x3359d8ff));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour (0xaa59d8ff));
        addAndMakeVisible (keyboard);

        nlog ("editor: keyboard done");
        // звёзды, облака, пылинки
        juce::Random r (11);
        for (int i = 0; i < 170; ++i)
            stars.push_back ({ r.nextFloat() * kW, r.nextFloat() * 390.f, 1.f + r.nextFloat() * 1.8f, r.nextFloat() * 6.28f });
        for (int i = 0; i < 7; ++i)
            clouds.push_back ({ r.nextFloat() * (kW + 300.f), 40.f + r.nextFloat() * 220.f, 0.7f + r.nextFloat() * 0.9f, 4.f + r.nextFloat() * 10.f });
        for (int i = 0; i < 55; ++i)
            motes.push_back ({ r.nextFloat() * kW, 80.f + r.nextFloat() * 300.f, 1.f + r.nextFloat() * 2.f, r.nextFloat() * 6.28f, 0.1f + r.nextFloat() * 0.35f });

        {
            float bx = 0.f;
            while (bx < kW)
            {
                Bld bl;
                bl.x = bx;
                bl.w = 26.f + r.nextFloat() * 34.f;
                bl.h = 60.f + r.nextFloat() * 150.f;
                bl.seed = (float) r.nextInt (1000);
                blds.push_back (bl);
                bx += bl.w + 2.f + r.nextFloat() * 6.f;
            }
        }

        for (auto& b : prevHeld) b = false;
        for (auto& b : beam) b = 0.f;

        nlog ("editor: stars done");
        // стартовый пресет
        const int nG = (int) genres().size();
        if (! proc.apvts.state.hasProperty ("genre"))
        {
            curGenre = loadedGenre = 0;
            loadedPreset = 0;
            curA = tgtA = genres()[0].a;
            curB = tgtB = genres()[0].b;
            refreshLists();
        }
        else
        {
            const int g = juce::jlimit (0, nG - 1, (int) proc.apvts.state.getProperty ("genre", 0));
            const int pn = (int) genres()[(size_t) g].presets.size();
            const int pr = juce::jlimit (0, pn - 1, (int) proc.apvts.state.getProperty ("preset", 0));
            curGenre = loadedGenre = g;
            loadedPreset = pr;
            curA = tgtA = genres()[(size_t) g].a;
            curB = tgtB = genres()[(size_t) g].b;
            refreshLists();
        }

        shownGenre = curGenre;
        nlog ("editor: preset done");
        resized();
        nlog ("editor: ctor end");
        startTimerHz (60);
    }

    ~NebulaEditor() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        static bool firstResize = true;
        if (firstResize) { firstResize = false; nlog ("resized first"); }
        prevBtn.setBounds (352, 14, 34, 30);
        nextBtn.setBounds (594, 14, 34, 30);
        skyBtn.setBounds ((int) kW - 160, 14, 140, 30);

        genreList.setBounds (14, 90, 250, 190);
        presetList.setBounds (14, 304, 250, 268);

        const float colW = 692.f / 6.f;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) (i % 6);
            const int row = (int) (i / 6);
            const float cx = 278.f + ((float) col + 0.5f) * colW;
            knobs[i]->s.setBounds ((int) (cx - 29.f), 412 + row * 80, 58, 58);
        }

        keyboard.setBounds (0, (int) kH - 80, (int) kW, 80);
    }

    //----------------------------------------------------------------------
    void timerCallback() override
    {
        static bool firstTick = true;
        if (firstTick) { firstTick = false; nlog ("timer first"); }
        const float dt = 1.f / 60.f;
        t += dt;

        float dayTarget;
        if (skyMode == 0)
        {
            autoPhase += dt / 55.f;
            dayTarget = 0.5f - 0.5f * std::cos (kTwoPi * autoPhase);
        }
        else
            dayTarget = skyMode == 2 ? 1.f : 0.f;
        day += (dayTarget - day) * 0.04f;

        lv += (proc.level.load() - lv) * 0.25f;
        curA = curA.interpolatedWith (tgtA, 0.06f);
        curB = curB.interpolatedWith (tgtB, 0.06f);
        look.accent = curA;

        if (shownGenre != curGenre)
        {
            sceneFade -= 0.09f;
            if (sceneFade <= 0.f) { sceneFade = 0.f; shownGenre = curGenre; }
        }
        else
            sceneFade = juce::jmin (1.f, sceneFade + 0.07f);

        // ноты
        for (int n = 0; n < 128; ++n)
        {
            const bool h = proc.noteState[(size_t) n].load();
            if (h && ! prevHeld[n])
                spawnNote (n);
            prevHeld[n] = h;
            beam[n] += ((h ? 1.f : 0.f) - beam[n]) * (h ? 0.35f : 0.07f);
        }

        // кольца вокруг шара
        ringTimer += dt;
        if (ringTimer > 0.9f - 0.5f * juce::jmin (1.f, lv * 3.f))
        {
            ringTimer = 0.f;
            rings.push_back ({ 24.f, 0.4f });
        }
        for (auto& r : rings) { r.r += 0.9f + lv * 3.f; r.a -= 0.0045f; }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.a <= 0.f; }), rings.end());

        for (auto& r : ripples) { r.r += 2.4f; r.a -= 0.013f; }
        ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.a <= 0.f; }), ripples.end());

        for (auto& s : sparks) { s.x += s.vx; s.y += s.vy; s.vy += 0.06f; s.life += 1.f; }
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life >= s.max; }), sparks.end());

        for (auto& m : motes)
        {
            m.ph += 0.02f;
            m.x += std::sin (m.ph) * 0.3f;
            m.y -= m.sp;
            if (m.y < 70.f) { m.y = 400.f; m.x = rnd.nextFloat() * kW; }
        }

        for (auto& s : stars)
            s.ph += 0.05f;

        repaint();
    }

    //----------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        static bool paintLogged = false;
        const bool logIt = ! paintLogged;
        paintLogged = true;
        if (logIt) nlog ("paint start");

        const float W = kW, H = kH;
        const float d = smooth01 (day);
        const juce::Colour A = curA, B = curB;

        // сцена жанра
        g.fillAll (juce::Colours::black);
        g.beginTransparencyLayer (sceneFade);
        drawScene (g, d);
        g.endTransparencyLayer();
        if (logIt) nlog ("paint scene done");

        // лучи нот, круги на земле, искры
        for (int n = 0; n < 128; ++n)
        {
            if (beam[n] < 0.02f) continue;
            const float x = noteX (n);
            const juce::Colour c = noteColour (n);
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.65f * beam[n]), x, kGroundY, c.withAlpha (0.f), x, kGroundY - 270.f, false));
            g.fillRect (x - 5.f, kGroundY - 270.f, 10.f, 270.f);
            g.setColour (c.withAlpha (0.9f * beam[n]));
            g.fillRect (x - 1.2f, kGroundY - 270.f * beam[n], 2.4f, 270.f * beam[n]);
        }
        for (auto& r : ripples)
        {
            g.setColour (r.c.withAlpha (juce::jlimit (0.f, 1.f, r.a)));
            g.drawEllipse (r.x - r.r, r.y - r.r * 0.3f, r.r * 2.f, r.r * 0.6f, 1.8f);
        }
        for (auto& s : sparks)
        {
            const float k = s.life / s.max;
            g.setColour (s.c.withAlpha ((1.f - k) * 0.35f));
            g.fillEllipse (s.x - s.size * 2.f, s.y - s.size * 2.f, s.size * 4.f, s.size * 4.f);
            g.setColour (juce::Colours::white.withAlpha (1.f - k));
            g.fillEllipse (s.x - s.size * 0.5f, s.y - s.size * 0.5f, s.size, s.size);
        }

        if (logIt) nlog ("paint notes done");
        // светлячки и пыльца
        for (auto& m : motes)
        {
            const float tw = 0.5f + 0.5f * std::sin (m.ph * 3.f);
            const juce::Colour mc = B.interpolatedWith (juce::Colours::white, d);
            g.setColour (mc.withAlpha (0.18f + 0.35f * tw));
            g.fillEllipse (m.x, m.y, m.s, m.s);
        }

        // виньетка
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                                 juce::Colours::black.withAlpha (0.4f), 0.f, 0.f, true));
        g.fillAll();

        if (logIt) nlog ("paint vignette done");
        // панели
        auto panel = [&] (juce::Rectangle<float> r)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 16.f);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.drawRoundedRectangle (r, 16.f, 1.2f);
        };
        panel ({ 8.f, 8.f, W - 16.f, 48.f });
        panel ({ 8.f, 64.f, 262.f, 512.f });
        panel ({ 278.f, 400.f, 692.f, 176.f });

        if (logIt) nlog ("paint panels done");
        // текст
        auto txt = [&] (const juce::String& s, juce::Rectangle<int> r, float size, bool bold, float kern, juce::Colour c,
                        juce::Justification j = juce::Justification::centred)
        {
            juce::Font f (size, bold ? juce::Font::bold : juce::Font::plain);
            f.setExtraKerningFactor (kern);
            g.setFont (f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawText (s, r.translated (0, 1), j);
            g.setColour (c);
            g.drawText (s, r, j);
        };

        txt ("NEBULA", { 24, 14, 200, 30 }, 24.f, true, 0.35f, juce::Colours::white, juce::Justification::centredLeft);
        txt ("SYNTH", { 150, 18, 80, 22 }, 11.f, false, 0.4f, A, juce::Justification::centredLeft);

        const auto& gd = genres()[(size_t) loadedGenre];
        txt (gd.name.toUpperCase(), { 392, 8, 196, 20 }, 11.f, true, 0.3f, A);
        txt (gd.presets[(size_t) loadedPreset].name, { 392, 24, 196, 28 }, 19.f, true, 0.05f, juce::Colours::white);

        txt ("FOLDERS", { 22, 70, 200, 16 }, 11.f, true, 0.4f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centredLeft);
        txt ((genres()[(size_t) curGenre].name + " PRESETS").toUpperCase(), { 22, 284, 240, 16 }, 11.f, true, 0.3f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centredLeft);

        const float colW = 692.f / 6.f;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) (i % 6);
            const int row = (int) (i / 6);
            const int cx = (int) (278.f + ((float) col + 0.5f) * colW);
            txt (knobs[i]->label, { cx - 50, 472 + row * 80, 100, 16 }, 11.f, true, 0.25f, juce::Colours::white.withAlpha (0.8f));
        }
        if (logIt) nlog ("paint end");
    }

private:
    struct Knob  { juce::Slider s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; juce::String label; };
    struct Star  { float x, y, s, ph; };
    struct Cloud { float x, y, s, sp; };
    struct Mote  { float x, y, s, ph, sp; };
    struct Ring  { float r, a; };
    struct Ripple { float x, y, r, a; juce::Colour c; };
    struct Spark { float x, y, vx, vy, life, max, size; juce::Colour c; };
    struct Bld   { float x, w, h, seed; };

    static constexpr float kW = 980.f;
    static constexpr float kH = 660.f;
    static constexpr float kOrbX = 623.f;
    static constexpr float kOrbY = 205.f;
    static constexpr float kGroundY = 384.f;

    static float noteX (int n) { return 296.f + juce::jlimit (0.f, 1.f, ((float) n - 36.f) / 60.f) * 660.f; }

    juce::Colour noteColour (int n) const
    {
        return curA.interpolatedWith (curB, juce::jlimit (0.f, 1.f, ((float) n - 36.f) / 60.f));
    }

    //----------------------------------------------------------------------
    // Сцены жанров
    //----------------------------------------------------------------------
    void drawAurora (juce::Graphics& g, float vis)
    {
        if (vis < 0.01f) return;
        const juce::Colour cols[3] = { curA, curB, curA.interpolatedWith (curB, 0.5f) };
        for (int b = 0; b < 3; ++b)
        {
            juce::Path p;
            const float base = 110.f + 50.f * (float) b;
            for (float x = 0.f; x <= kW + 16.f; x += 16.f)
            {
                const float y = base + 24.f * std::sin (x * 0.012f + t * 0.5f + (float) b * 2.f)
                                     + 14.f * std::sin (x * 0.027f - t * 0.8f + (float) b);
                if (x == 0.f) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            for (float wd = 46.f; wd >= 8.f; wd -= 12.f)
            {
                g.setColour (cols[b].withAlpha (vis * 0.05f));
                g.strokePath (p, juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    void drawScene (juce::Graphics& g, float d)
    {
        struct Pal { juce::uint32 nt, nb, dt, db; };
        static const Pal pals[8] = {
            { 0xff04061a, 0xff12123a, 0xff6fd3ff, 0xffffd1f2 },
            { 0xff1a1030, 0xff3a1f5c, 0xffffc4ec, 0xffcffff0 },
            { 0xff0e1a3a, 0xff2a3f6e, 0xff8fd0ff, 0xfffff3d6 },
            { 0xff06030c, 0xff1c0a22, 0xff3a2a4a, 0xff8a5a7a },
            { 0xff1a0f2e, 0xff5a2a4a, 0xffffb36b, 0xffffe0a0 },
            { 0xff0a0204, 0xff3a0808, 0xff5a2a1a, 0xffff8a3a },
            { 0xff120a2a, 0xff2a1052, 0xffff9ee8, 0xffa8fff0 },
            { 0xff0a0f2a, 0xff2a2060, 0xffb8a8ff, 0xffffd8f0 } };
        const Pal& p = pals[juce::jlimit (0, 7, shownGenre)];

        g.setGradientFill (juce::ColourGradient (juce::Colour (p.nt).interpolatedWith (juce::Colour (p.dt), d), 0.f, 0.f,
                                                 juce::Colour (p.nb).interpolatedWith (juce::Colour (p.db), d), 0.f, 400.f, false));
        g.fillAll();

        if (d < 0.97f)
            for (auto& s : stars)
            {
                const float tw = 0.5f + 0.5f * std::sin (s.ph);
                g.setColour (juce::Colour (0xffe8f0ff).withAlpha ((1.f - d) * (0.3f + 0.7f * tw)));
                g.fillEllipse (s.x - s.s * 0.5f, s.y - s.s * 0.5f, s.s, s.s);
            }

        static bool sceneLogged = false;
        if (! sceneLogged) { sceneLogged = true; nlog ("drawScene genre " + juce::String (shownGenre)); }

        switch (shownGenre)
        {
            case 0:  sceneGlo (g, d);      break;
            case 1:  scenePlugg (g, d);    break;
            case 2:  sceneDairy (g, d);    break;
            case 3:  sceneDark (g, d);     break;
            case 4:  scenePluggnb (g, d);  break;
            case 5:  sceneRage (g, d);     break;
            case 6:  sceneHyper (g, d);    break;
            default: sceneDream (g, d);    break;
        }
    }

    // 0: неоновый город будущего
    void sceneGlo (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA, B = curB;
        const float lvl = juce::jmin (1.f, lv * 2.f);
        drawAurora (g, (1.f - d) * (1.f - d));

        const float px = 700.f, py = 150.f + 8.f * std::sin (t * 0.3f);
        g.setGradientFill (juce::ColourGradient (A.brighter (0.4f), px - 30.f, py - 30.f, B.darker (0.4f), px + 44.f, py + 44.f, false));
        g.fillEllipse (px - 50.f, py - 50.f, 100.f, 100.f);
        {
            juce::Path ring;
            ring.addEllipse (px - 92.f, py - 14.f, 184.f, 28.f);
            ring.applyTransform (juce::AffineTransform::rotation (-0.35f, px, py));
            g.setColour (A.withAlpha (0.3f));
            g.strokePath (ring, juce::PathStrokeType (9.f));
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.strokePath (ring, juce::PathStrokeType (2.5f));
        }

        for (int i = 0; i < 10; ++i)
        {
            const float x = 300.f + hash1 (i) * 650.f;
            const float y = 80.f + std::fmod (hash1 (i + 40) * 220.f + t * (6.f + 8.f * hash1 (i + 9)), 230.f);
            const float s = 4.f + 7.f * hash1 (i + 77);
            juce::Path dm;
            dm.startNewSubPath (x, y - s * 1.5f);
            dm.lineTo (x + s, y);
            dm.lineTo (x, y + s * 1.5f);
            dm.lineTo (x - s, y);
            dm.closeSubPath();
            g.setColour ((i % 2 ? A : B).withAlpha (0.5f));
            g.fillPath (dm);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.strokePath (dm, juce::PathStrokeType (1.f));
        }

        const float baseY = 392.f;
        for (auto& b : blds)
        {
            const float top = baseY - b.h;
            const juce::Colour bcol = juce::Colour (0xff0a0d2a).interpolatedWith (A.darker (2.4f), 0.2f)
                                          .interpolatedWith (juce::Colour (0xff9fc8ea), d * 0.85f);
            g.setColour (bcol);
            g.fillRect (b.x, top, b.w, b.h);
            g.setColour (A.withAlpha (0.8f - 0.4f * d));
            g.drawLine (b.x, top, b.x + b.w, top, 1.5f);

            for (float yy = top + 8.f; yy < baseY - 6.f; yy += 9.f)
                for (float xx = b.x + 4.f; xx < b.x + b.w - 5.f; xx += 8.f)
                {
                    const float hv = hash1 ((int) (b.seed * 31.f + yy * 7.f + xx * 3.f));
                    const float thr = 0.58f - 0.28f * lvl + 0.1f * std::sin (t * 0.8f + hv * 20.f);
                    if (hv > thr)
                    {
                        g.setColour ((hv > 0.8f ? B : A).withAlpha (0.8f * (1.f - 0.55f * d)));
                        g.fillRect (xx, yy, 3.5f, 4.5f);
                    }
                }

            if (((int) b.seed) % 3 == 0)
            {
                g.setColour (bcol.brighter (0.3f));
                g.drawLine (b.x + b.w * 0.5f, top, b.x + b.w * 0.5f, top - 8.f, 1.f);
                g.setColour (juce::Colour (0xffff4060).withAlpha (0.5f + 0.5f * std::sin (t * 3.f + b.seed)));
                g.fillEllipse (b.x + b.w * 0.5f - 2.f, top - 11.f, 4.f, 4.f);
            }
        }
        g.setColour (A.withAlpha (0.85f));
        g.fillRect (0.f, baseY, kW, 2.f);
    }

    // 1: пастельный candy-мир
    void scenePlugg (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA, B = curB;

        for (int i = 0; i < 7; ++i)
        {
            const float r = 300.f - (float) i * 11.f;
            juce::Path arc;
            arc.addCentredArc (640.f, 440.f, r, r, 0.f, -kPi * 0.5f, kPi * 0.5f, true);
            g.setColour (juce::Colour::fromHSV ((float) i / 7.f, 0.45f, 1.f, 0.16f + 0.1f * d));
            g.strokePath (arc, juce::PathStrokeType (10.f));
        }

        for (int i = 0; i < 5; ++i)
        {
            const float x = std::fmod ((float) i * 210.f + t * (5.f + 3.f * (float) i), kW + 400.f) - 200.f;
            puff (g, x, 110.f + (float) (i % 3) * 60.f, 1.1f, juce::Colours::white.interpolatedWith (A, 0.3f), 0.2f + 0.25f * d);
        }

        hillLayer (g, kW, kH, 366.f, 24.f, 0.008f, 0.5f, juce::Colour (0xff2a1850).interpolatedWith (juce::Colour (0xffbff0d8), d));
        hillLayer (g, kW, kH, 382.f, 18.f, 0.012f, 2.f,  juce::Colour (0xff1e1240).interpolatedWith (juce::Colour (0xffa0e4c4), d));

        for (int i = 0; i < 8; ++i)
        {
            const float x = 300.f + hash1 (i) * 650.f + std::sin (t + (float) i) * 12.f;
            const float y = 400.f - std::fmod (t * (14.f + 10.f * hash1 (i + 5)) + hash1 (i + 11) * 330.f, 330.f);
            const float s = 7.f + 8.f * hash1 (i + 3);
            const auto hp = heartPath (x, y, s);
            g.setColour ((i % 2 ? A : B).withAlpha (0.6f));
            g.fillPath (hp);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.strokePath (hp, juce::PathStrokeType (1.2f));
        }

        for (int i = 0; i < 16; ++i)
        {
            const float tw = 0.5f + 0.5f * std::sin (t * 2.f + (float) i * 1.7f);
            sparkle (g, 290.f + hash1 (i + 200) * 670.f, 80.f + hash1 (i + 300) * 280.f, 3.f + 5.f * tw, juce::Colours::white.withAlpha (0.4f + 0.5f * tw));
        }
    }

    // 2: воздушный Dairy Plugg
    void sceneDairy (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA;
        const float sx = 820.f, sy = 110.f + 20.f * d;

        if (d > 0.05f)
            for (int i = 0; i < 9; ++i)
            {
                const float a0 = t * 0.05f + (float) i * kTwoPi / 9.f;
                juce::Path ray;
                ray.startNewSubPath (sx, sy);
                ray.lineTo (sx + std::cos (a0) * 900.f, sy + std::sin (a0) * 900.f);
                ray.lineTo (sx + std::cos (a0 + 0.12f) * 900.f, sy + std::sin (a0 + 0.12f) * 900.f);
                ray.closeSubPath();
                g.setColour (juce::Colours::white.withAlpha (0.1f * d));
                g.fillPath (ray);
            }
        drawBody (g, sx, 110.f, 130.f, 34.f, juce::Colour (0xffdfe8ff), juce::Colour (0xfffff2b0), d);

        const juce::Colour cloudCol = juce::Colour (0xff8f9ae0).interpolatedWith (juce::Colours::white, d);
        for (int layer = 0; layer < 3; ++layer)
            for (int i = 0; i < 4; ++i)
            {
                const float sp = 3.f + 4.f * (float) layer;
                const float x = std::fmod ((float) i * 300.f + (float) layer * 90.f + t * sp, kW + 500.f) - 250.f;
                const float y = 90.f + (float) layer * 70.f + 18.f * std::sin ((float) i * 2.1f + t * 0.2f);
                puff (g, x, y, 1.f + 0.5f * (float) layer, cloudCol, (0.3f + 0.12f * (float) layer) * (0.6f + 0.4f * d));
            }

        static const juce::uint32 bc[3] = { 0xffff8fb8, 0xff8fd8ff, 0xffffe08a };
        for (int i = 0; i < 3; ++i)
        {
            const float bx = 430.f + (float) i * 190.f + 12.f * std::sin (t * 0.4f + (float) i);
            const float by = 190.f - 20.f * (float) (i % 2) + 14.f * std::sin (t * 0.6f + (float) i * 1.7f);
            const juce::Colour c (bc[i]);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawLine (bx - 10.f, by + 34.f, bx - 6.f, by + 56.f, 1.f);
            g.drawLine (bx + 10.f, by + 34.f, bx + 6.f, by + 56.f, 1.f);
            g.setGradientFill (juce::ColourGradient (c.brighter (0.4f), bx - 10.f, by - 18.f, c.darker (0.2f), bx + 18.f, by + 30.f, false));
            g.fillEllipse (bx - 22.f, by - 28.f, 44.f, 58.f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawEllipse (bx - 22.f, by - 28.f, 44.f, 58.f, 1.f);
            g.drawLine (bx, by - 28.f, bx, by + 30.f, 1.f);
            g.setColour (juce::Colour (0xff8a5a3a));
            g.fillRect (bx - 7.f, by + 56.f, 14.f, 10.f);
        }

        for (int i = 0; i < 6; ++i)
        {
            const float x = std::fmod ((float) i * 170.f + t * (22.f + 4.f * (float) i), kW + 100.f) - 50.f;
            const float y = 120.f + 28.f * std::sin ((float) i * 1.9f) + 8.f * std::sin (t * 1.3f + (float) i);
            const float flap = std::sin (t * 7.f + (float) i * 2.f) * 5.f;
            juce::Path bird;
            bird.startNewSubPath (x - 9.f, y - flap);
            bird.quadraticTo (x - 4.f, y - 4.f + flap * 0.3f, x, y);
            bird.quadraticTo (x + 4.f, y - 4.f + flap * 0.3f, x + 9.f, y - flap);
            g.setColour (juce::Colour (0xff3a4a7a).withAlpha (0.25f + 0.45f * d));
            g.strokePath (bird, juce::PathStrokeType (1.8f));
        }

        hillLayer (g, kW, kH, 368.f, 26.f, 0.008f, 0.5f, juce::Colour (0xff1c2b55).interpolatedWith (juce::Colour (0xffc7efd2), d));
        hillLayer (g, kW, kH, 382.f, 20.f, 0.011f, 2.1f, juce::Colour (0xff15204a).interpolatedWith (juce::Colour (0xffa8e0b8), d));

        for (int i = 0; i < 14; ++i)
        {
            const float x = std::fmod (hash1 (i) * kW + t * (8.f + 6.f * hash1 (i + 3)), kW);
            const float y = 150.f + hash1 (i + 20) * 220.f + 12.f * std::sin (t * 0.8f + (float) i);
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            for (int k = 0; k < 6; ++k)
            {
                const float a = (float) k * kTwoPi / 6.f + t * 0.3f;
                g.drawLine (x, y, x + std::cos (a) * 5.f, y + std::sin (a) * 5.f, 0.8f);
            }
            g.fillEllipse (x - 1.2f, y - 1.2f, 2.4f, 2.4f);
            g.drawLine (x, y, x - 3.f, y + 9.f, 0.8f);
        }
        juce::ignoreUnused (A);
    }

    // 3: готический замок и кровавая луна
    void sceneDark (juce::Graphics& g, float d)
    {
        drawBody (g, 690.f, 170.f, 190.f, 62.f, juce::Colour (0xffc22a3e), juce::Colour (0xffcdbfe0), d);

        const juce::Colour sil = juce::Colour (0xff07040b).interpolatedWith (juce::Colour (0xff2a2038), d);
        const float cx = 690.f, gy = 372.f;
        g.setColour (sil);
        g.fillRect (cx - 80.f, gy - 110.f, 160.f, 140.f);
        auto tower = [&] (float x, float w, float h)
        {
            g.fillRect (x - w * 0.5f, gy - h, w, h + 20.f);
            juce::Path roof;
            roof.startNewSubPath (x - w * 0.62f, gy - h);
            roof.lineTo (x, gy - h - w * 1.4f);
            roof.lineTo (x + w * 0.62f, gy - h);
            roof.closeSubPath();
            g.fillPath (roof);
        };
        tower (cx - 90.f, 34.f, 150.f);
        tower (cx + 90.f, 34.f, 170.f);
        tower (cx, 40.f, 190.f);
        tower (cx - 45.f, 22.f, 135.f);
        for (int i = 0; i < 8; ++i)
            g.fillRect (cx - 78.f + (float) i * 21.f, gy - 120.f, 12.f, 12.f);

        const float wins[7][2] = { { -90.f, -110.f }, { 90.f, -130.f }, { 0.f, -150.f }, { 0.f, -95.f },
                                   { -45.f, -95.f }, { 45.f, -70.f }, { -90.f, -60.f } };
        for (int i = 0; i < 7; ++i)
        {
            const float fl = 0.6f + 0.4f * std::sin (t * (2.f + (float) i) + (float) i) + lv;
            g.setColour (juce::Colour (0xffffa040).withAlpha (juce::jlimit (0.f, 1.f, fl)));
            g.fillRoundedRectangle (cx + wins[i][0] - 3.f, gy + wins[i][1], 6.f, 12.f, 3.f);
        }

        hillLayer (g, kW, kH, 372.f, 22.f, 0.009f, 1.f, sil);

        g.setColour (sil);
        branch (g, 350.f, 392.f, 0.f, 62.f, 5, 7.f);
        branch (g, 930.f, 392.f, 0.1f, 52.f, 5, 6.f);

        for (int i = 0; i < 10; ++i)
        {
            const float x = 420.f + (float) i * 50.f + hash1 (i) * 20.f;
            g.fillRect (x, 372.f, 3.f, 16.f);
            g.fillRect (x - 4.f, 376.f, 11.f, 3.f);
        }

        hillLayer (g, kW, kH, 388.f, 14.f, 0.014f, 2.f, sil.darker (0.4f));

        for (int i = 0; i < 6; ++i)
        {
            const float x = std::fmod ((float) i * 160.f + t * (30.f + 6.f * (float) i), kW + 80.f) - 40.f;
            const float y = 100.f + 40.f * std::sin (t * 0.9f + (float) i * 1.7f) + (float) (i % 3) * 25.f;
            const float fl = std::sin (t * 9.f + (float) i) * 6.f;
            juce::Path w;
            w.startNewSubPath (x, y);
            w.quadraticTo (x - 8.f, y - 8.f - fl, x - 16.f, y - fl * 0.3f);
            w.quadraticTo (x - 10.f, y + 1.f, x - 6.f, y + 3.f);
            w.quadraticTo (x - 2.f, y + 1.f, x, y + 4.f);
            w.quadraticTo (x + 2.f, y + 1.f, x + 6.f, y + 3.f);
            w.quadraticTo (x + 10.f, y + 1.f, x + 16.f, y - fl * 0.3f);
            w.quadraticTo (x + 8.f, y - 8.f - fl, x, y);
            w.closeSubPath();
            g.setColour (juce::Colour (0xff05030a).withAlpha (0.9f));
            g.fillPath (w);
        }

        for (int i = 0; i < 5; ++i)
            puff (g, std::fmod ((float) i * 260.f + t * 6.f, kW + 400.f) - 200.f, 352.f + (float) (i % 2) * 14.f, 1.7f, juce::Colour (0xffb9a0d8), 0.16f);
    }

    // 4: закат на пляже
    void scenePluggnb (juce::Graphics& g, float d)
    {
        const juce::Colour B = curB;
        const float hy = 326.f;
        drawBody (g, 640.f, 150.f, 292.f, 46.f, juce::Colour (0xfff4d8ff), juce::Colour (0xffff8a3c), d);

        for (int i = 0; i < 5; ++i)
        {
            const float x = std::fmod ((float) i * 240.f + t * (6.f + (float) i), kW + 400.f) - 200.f;
            const float y = 100.f + (float) i * 28.f;
            g.setGradientFill (juce::ColourGradient (B.withAlpha (0.35f), x, y, B.withAlpha (0.f), x + 150.f, y, true));
            g.fillEllipse (x - 150.f, y - 9.f, 300.f, 18.f);
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2a5a).interpolatedWith (juce::Colour (0xffff9a5a), d), 0.f, hy,
                                                 juce::Colour (0xff0e0f2a).interpolatedWith (juce::Colour (0xff2a7a9a), d), 0.f, kH, false));
        g.fillRect (0.f, hy, kW, kH - hy);

        const juce::Colour rc = juce::Colour (0xfff4d8ff).interpolatedWith (juce::Colour (0xffffd08a), d);
        for (int i = 0; i < 16; ++i)
        {
            const float y = hy + 3.f + (float) i * 4.5f;
            const float w = (14.f + (float) i * 6.f) * (0.7f + 0.3f * std::sin (t * 2.f + (float) i * 1.3f));
            g.setColour (rc.withAlpha (0.55f - (float) i * 0.025f));
            g.fillRoundedRectangle (640.f - w * 0.5f, y, w, 2.f, 1.f);
        }
        for (int i = 0; i < 24; ++i)
        {
            const float al = 0.15f + 0.3f * (0.5f + 0.5f * std::sin (t * 1.5f + (float) i));
            g.setColour (juce::Colours::white.withAlpha (al));
            g.fillRect (hash1 (i + 400) * kW, hy + 6.f + hash1 (i + 500) * 58.f, 8.f + 10.f * hash1 (i + 600), 1.5f);
        }

        auto palm = [&] (float x, float y, float lean, float s)
        {
            g.setColour (juce::Colour (0xff0b0614));
            juce::Path trunk;
            trunk.startNewSubPath (x - 4.f * s, y);
            trunk.quadraticTo (x + lean * 0.5f, y - 90.f * s, x + lean, y - 150.f * s);
            trunk.lineTo (x + lean + 5.f * s, y - 150.f * s);
            trunk.quadraticTo (x + lean * 0.5f + 7.f * s, y - 90.f * s, x + 5.f * s, y);
            trunk.closeSubPath();
            g.fillPath (trunk);
            const float tx = x + lean, ty = y - 150.f * s;
            for (int k = 0; k < 7; ++k)
            {
                const float ang = -kPi + ((float) k + 0.5f) * kPi / 7.f + std::sin (t * 0.8f + (float) k) * 0.04f;
                const float dx = std::cos (ang), dy = std::sin (ang);
                juce::Path leaf;
                leaf.startNewSubPath (tx, ty);
                leaf.quadraticTo (tx + dx * 40.f * s, ty + dy * 40.f * s - 14.f * s, tx + dx * 78.f * s, ty + dy * 26.f * s + 26.f * s);
                g.strokePath (leaf, juce::PathStrokeType (5.f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        };
        palm (340.f, 394.f, 30.f, 1.f);
        palm (930.f, 394.f, -34.f, 1.15f);

        g.setColour (juce::Colour (0xff1a1020).interpolatedWith (juce::Colour (0xffe8c08a), d));
        g.fillRect (0.f, 390.f, kW, 12.f);
    }

    // 5: вулкан и молнии
    void sceneRage (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA, B = curB;
        const float lvl = juce::jmin (1.f, lv * 2.5f);
        drawBody (g, 760.f, 160.f, 170.f, 58.f, juce::Colour (0xffb01818), juce::Colour (0xffffb04a), d);

        for (int i = 0; i < 5; ++i)
            puff (g, std::fmod ((float) i * 230.f + t * 8.f, kW + 400.f) - 200.f, 90.f + (float) (i % 3) * 30.f, 1.4f,
                  juce::Colour (0xff3a0d0d).interpolatedWith (juce::Colour (0xff6a4a3a), d), 0.45f);

        auto ridge = [&] (float base, float amp, int seed, juce::Colour c)
        {
            juce::Path p;
            p.startNewSubPath (0.f, kH);
            for (int i = 0; i <= 36; ++i)
                p.lineTo ((float) i * 28.f, base - amp * hash1 (seed + i));
            p.lineTo (kW + 28.f, kH);
            p.closeSubPath();
            g.setColour (c);
            g.fillPath (p);
        };
        const juce::Colour rock = juce::Colour (0xff120507).interpolatedWith (juce::Colour (0xff3a1a14), d);
        ridge (330.f, 70.f, 3, rock.brighter (0.15f));

        juce::Path vol;
        vol.startNewSubPath (530.f, 400.f);
        vol.lineTo (650.f, 205.f);
        vol.lineTo (735.f, 205.f);
        vol.lineTo (850.f, 400.f);
        vol.closeSubPath();
        g.setColour (rock);
        g.fillPath (vol);

        g.setGradientFill (juce::ColourGradient (B.withAlpha (0.85f), 692.f, 205.f, A.withAlpha (0.f), 692.f + 90.f + 60.f * lvl, 205.f, true));
        g.fillEllipse (692.f - 150.f, 205.f - 150.f, 300.f, 300.f);

        juce::Path l1, l2;
        l1.startNewSubPath (675.f, 208.f); l1.lineTo (660.f, 260.f); l1.lineTo (668.f, 300.f); l1.lineTo (640.f, 360.f);
        l2.startNewSubPath (705.f, 208.f); l2.lineTo (718.f, 270.f); l2.lineTo (706.f, 320.f); l2.lineTo (732.f, 380.f);
        for (auto* lp : { &l1, &l2 })
        {
            g.setColour (A.withAlpha (0.3f));
            g.strokePath (*lp, juce::PathStrokeType (10.f));
            g.setColour (B.withAlpha (0.95f));
            g.strokePath (*lp, juce::PathStrokeType (3.5f));
        }

        for (int i = 0; i < 34; ++i)
        {
            const float ph = std::fmod (t * (0.5f + 0.5f * lvl) + hash1 (i) * 3.f, 1.f);
            const float x = 692.f + (hash1 (i + 50) - 0.5f) * 220.f * ph;
            const float y = 205.f - (260.f + 160.f * hash1 (i + 90)) * ph + 380.f * ph * ph;
            const float sz = 4.f * (1.f - ph * 0.5f);
            g.setColour ((i % 2 ? B : A).withAlpha ((1.f - ph) * 0.9f));
            g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        }

        ridge (372.f, 40.f, 7, rock.darker (0.2f));
        ridge (388.f, 22.f, 19, rock.darker (0.5f));
        g.setGradientFill (juce::ColourGradient (A.withAlpha (0.f), 0.f, 340.f, A.withAlpha (0.45f), 0.f, 400.f, false));
        g.fillRect (0.f, 340.f, kW, 60.f);

        if (lvl > 0.45f)
        {
            const int k = (int) (t * 12.f);
            const float lx = 320.f + hash1 (k) * 620.f;
            juce::Path bolt;
            bolt.startNewSubPath (lx, 64.f);
            float x = lx, y = 64.f;
            for (int i = 0; i < 7; ++i)
            {
                x += (hash1 (k * 7 + i) - 0.5f) * 60.f;
                y += 44.f;
                bolt.lineTo (x, y);
            }
            g.setColour (B.withAlpha (0.3f));
            g.strokePath (bolt, juce::PathStrokeType (7.f));
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.strokePath (bolt, juce::PathStrokeType (2.2f));
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (0.f, 0.f, kW, kH);
        }
    }

    // 6: хайперпоп y2k
    void sceneHyper (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA, B = curB;
        const float lvl = juce::jmin (1.f, lv * 2.f);
        const float vx = 640.f, vy = 330.f;

        g.setGradientFill (juce::ColourGradient (B.withAlpha (0.f), 0.f, vy, B.darker (1.f).withAlpha (0.5f), 0.f, kH, false));
        g.fillRect (0.f, vy, kW, kH - vy);
        g.setColour (A.withAlpha (0.6f));
        for (int i = -14; i <= 14; ++i)
            g.drawLine (vx, vy, vx + (float) i * 110.f, kH, 1.2f);
        for (int k = 0; k < 8; ++k)
        {
            const float q = ((float) k + std::fmod (t * 0.8f, 1.f)) / 8.f;
            g.drawLine (0.f, vy + 330.f * q * q, kW, vy + 330.f * q * q, 1.2f);
        }

        // хромовая звезда
        {
            juce::Path st;
            for (int i = 0; i < 10; ++i)
            {
                const float r = (i % 2 == 0) ? 74.f : 32.f;
                const float a = t * 0.4f + (float) i * kPi / 5.f - kPi * 0.5f;
                const float x = 640.f + std::cos (a) * r, y = 175.f + std::sin (a) * r;
                if (i == 0) st.startNewSubPath (x, y); else st.lineTo (x, y);
            }
            st.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, 600.f, 135.f, B, 690.f, 225.f, false));
            g.fillPath (st);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.strokePath (st, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
            g.setColour (A.withAlpha (0.25f));
            g.strokePath (st, juce::PathStrokeType (12.f, juce::PathStrokeType::curved));
        }

        // хромовые шары
        for (int i = 0; i < 5; ++i)
        {
            const float x = 330.f + hash1 (i + 60) * 620.f;
            const float y = 110.f + 70.f * hash1 (i + 70) + 14.f * std::sin (t * 0.9f + (float) i * 1.3f);
            const float r = 14.f + 18.f * hash1 (i + 80);
            const juce::Colour c = (i % 2 ? A : B);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, x - r * 0.4f, y - r * 0.4f, c.darker (0.4f), x + r * 0.8f, y + r * 0.8f, true));
            g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawEllipse (x - r, y - r, r * 2.f, r * 2.f, 1.f);
        }

        // кольца и треугольники
        for (int i = 0; i < 3; ++i)
        {
            const float x = 380.f + (float) i * 280.f, y = 290.f - (float) (i % 2) * 130.f + 10.f * std::sin (t + (float) i);
            const float r = 28.f + (float) i * 6.f;
            g.setColour ((i % 2 ? A : B).withAlpha (0.8f));
            g.drawEllipse (x - r, y - r * 0.45f, r * 2.f, r * 0.9f, 4.f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawEllipse (x - r, y - r * 0.45f, r * 2.f, r * 0.9f, 1.2f);
        }
        for (int i = 0; i < 3; ++i)
        {
            const float x = 450.f + (float) i * 230.f, y = 230.f + 22.f * std::sin (t * 0.7f + (float) i * 2.f);
            juce::Path tri;
            for (int k = 0; k < 3; ++k)
            {
                const float a = t * (0.6f + 0.2f * (float) i) + (float) k * kTwoPi / 3.f;
                const float px = x + std::cos (a) * 20.f, py = y + std::sin (a) * 20.f;
                if (k == 0) tri.startNewSubPath (px, py); else tri.lineTo (px, py);
            }
            tri.closeSubPath();
            g.setColour ((i % 2 ? B : A).withAlpha (0.55f));
            g.fillPath (tri);
            g.setColour (juce::Colours::white);
            g.strokePath (tri, juce::PathStrokeType (1.5f));
        }

        for (int i = 0; i < 12; ++i)
        {
            const float tw = 0.5f + 0.5f * std::sin (t * 3.f + (float) i * 2.1f);
            sparkle (g, 290.f + hash1 (i + 700) * 670.f, 75.f + hash1 (i + 800) * 250.f, 3.f + 5.f * tw, juce::Colours::white.withAlpha (0.4f + 0.5f * tw));
        }

        // глитч
        const int gk = (int) (t * 8.f);
        if (lvl > 0.2f || hash1 (gk) > 0.82f)
            for (int i = 0; i < 4; ++i)
            {
                const float y = 70.f + hash1 (gk * 5 + i) * 320.f;
                const float x = 200.f + hash1 (gk * 7 + i) * 600.f;
                g.setColour ((i % 2 ? A : B).withAlpha (0.35f));
                g.fillRect (x, y, 80.f + hash1 (gk + i) * 280.f, 3.f + hash1 (gk + 2 * i) * 8.f);
            }
        juce::ignoreUnused (d);
    }

    // 7: плавающие острова и дверь
    void sceneDream (juce::Graphics& g, float d)
    {
        const juce::Colour A = curA, B = curB;
        drawAurora (g, (1.f - d) * 0.8f);
        drawBody (g, 640.f, 150.f, 190.f, 52.f, juce::Colour (0xfff2f0ff), juce::Colour (0xffffe6f6), d);

        for (int k = 0; k < 2; ++k)
        {
            const float s = std::fmod (t * 0.3f + (float) k * 0.5f, 1.f);
            const float hx = 930.f - 560.f * s, hy = 80.f + 170.f * s;
            const float al = std::sin (s * kPi);
            juce::Path trail;
            trail.startNewSubPath (hx, hy);
            trail.lineTo (hx + 110.f, hy - 38.f);
            trail.lineTo (hx + 108.f, hy - 34.f);
            trail.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.9f * al), hx, hy, juce::Colours::white.withAlpha (0.f), hx + 110.f, hy - 38.f, false));
            g.fillPath (trail);
            sparkle (g, hx, hy, 6.f, juce::Colours::white.withAlpha (al));
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1240).interpolatedWith (juce::Colour (0xffffd0ec), d), 0.f, 340.f,
                                                 juce::Colour (0xff0a0828).interpolatedWith (juce::Colour (0xffc8b0ff), d), 0.f, kH, false));
        g.fillRect (0.f, 340.f, kW, kH - 340.f);
        for (int i = 0; i < 14; ++i)
        {
            const float y = 344.f + (float) i * 4.f;
            const float w = (10.f + (float) i * 6.f) * (0.7f + 0.3f * std::sin (t * 1.6f + (float) i));
            g.setColour (juce::Colours::white.withAlpha (0.45f - (float) i * 0.025f));
            g.fillRoundedRectangle (640.f - w * 0.5f, y, w, 2.f, 1.f);
        }

        auto island = [&] (float x, float y, float s, juce::Colour grass)
        {
            juce::Path rock;
            rock.startNewSubPath (x - 70.f * s, y);
            rock.quadraticTo (x - 50.f * s, y + 40.f * s, x - 10.f * s, y + 90.f * s);
            rock.quadraticTo (x, y + 100.f * s, x + 12.f * s, y + 88.f * s);
            rock.quadraticTo (x + 55.f * s, y + 40.f * s, x + 70.f * s, y);
            rock.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a8a), x, y, juce::Colour (0xff2a2048), x, y + 100.f * s, false));
            g.fillPath (rock);
            g.setColour (grass);
            g.fillEllipse (x - 74.f * s, y - 12.f * s, 148.f * s, 26.f * s);
        };

        const float b1 = 6.f * std::sin (t * 0.5f), b2 = 6.f * std::sin (t * 0.6f + 2.f), b3 = 5.f * std::sin (t * 0.7f + 4.f);
        const juce::Colour grass = juce::Colour (0xff8fe0c0).interpolatedWith (juce::Colour (0xffb8f0d0), d);

        island (430.f, 250.f + b1, 1.f, grass);
        {
            const float x = 430.f, y = 250.f + b1;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe9a8).withAlpha (0.55f), x, y - 40.f, juce::Colour (0xffffe9a8).withAlpha (0.f), x, y - 150.f, false));
            g.fillRect (x - 16.f, y - 150.f, 32.f, 110.f);
            g.setColour (juce::Colour (0xff2a2048));
            g.fillRoundedRectangle (x - 12.f, y - 56.f, 24.f, 50.f, 12.f);
            g.setColour (juce::Colour (0xffffe9a8));
            g.fillRoundedRectangle (x - 8.f, y - 52.f, 16.f, 46.f, 8.f);
            g.setColour (juce::Colour (0xff3a2a58));
            g.fillRect (x + 36.f, y - 30.f, 4.f, 22.f);
            g.setColour (B.withAlpha (0.85f));
            g.fillEllipse (x + 26.f, y - 56.f, 24.f, 24.f);
        }

        island (850.f, 220.f + b2, 0.75f, grass);
        {
            const float x = 850.f, y = 220.f + b2;
            g.setColour (juce::Colour (0xffe8d8f0));
            g.fillRect (x - 16.f, y - 30.f, 32.f, 22.f);
            juce::Path roof;
            roof.startNewSubPath (x - 22.f, y - 30.f);
            roof.lineTo (x, y - 52.f);
            roof.lineTo (x + 22.f, y - 30.f);
            roof.closeSubPath();
            g.setColour (A.darker (0.2f));
            g.fillPath (roof);
            g.setColour (juce::Colour (0xffffd070).withAlpha (0.7f + 0.3f * std::sin (t * 2.f)));
            g.fillRect (x - 5.f, y - 24.f, 8.f, 9.f);
        }

        island (690.f, 305.f + b3, 0.5f, grass);
        g.setColour (juce::Colour (0xff3a2a58));
        g.fillRect (688.f, 270.f + b3, 4.f, 24.f);
        g.setColour (A.withAlpha (0.9f));
        g.fillEllipse (676.f, 248.f + b3, 28.f, 28.f);

        for (int i = 0; i < 4; ++i)
            puff (g, std::fmod ((float) i * 300.f + t * 5.f, kW + 400.f) - 200.f, 345.f + (float) (i % 2) * 10.f, 1.6f, juce::Colours::white, 0.12f + 0.08f * d);
    }

    void spawnNote (int n)
    {
        const float x = noteX (n);
        const juce::Colour c = noteColour (n);
        ripples.push_back ({ x, kGroundY, 6.f, 0.85f, c });
        for (int i = 0; i < 14; ++i)
            sparks.push_back ({ x + (rnd.nextFloat() - 0.5f) * 8.f, kGroundY,
                                (rnd.nextFloat() - 0.5f) * 2.4f, -(1.5f + rnd.nextFloat() * 3.f),
                                0.f, 40.f + rnd.nextFloat() * 40.f, 1.5f + rnd.nextFloat() * 2.f, c });
        if (ripples.size() > 80) ripples.erase (ripples.begin(), ripples.begin() + 20);
        if (sparks.size() > 600) sparks.erase (sparks.begin(), sparks.begin() + 150);
    }

    void loadPreset (int g, int p)
    {
        curGenre = g;
        loadedGenre = g;
        loadedPreset = p;
        proc.applyPreset (g, p);
        refreshLists();
    }

    void refreshLists()
    {
        tgtA = genres()[(size_t) curGenre].a;
        tgtB = genres()[(size_t) curGenre].b;
        genreList.updateContent();
        genreList.selectRow (curGenre, true, true);
        presetList.updateContent();
        if (loadedGenre == curGenre)
            presetList.selectRow (loadedPreset, false, true);
        else
            presetList.deselectAllRows();
        repaint();
    }

    void step (int dir)
    {
        const auto& gs = genres();
        const int nG = (int) gs.size();
        int g = loadedGenre;
        int p = loadedPreset + dir;
        if (p >= (int) gs[(size_t) g].presets.size())
        {
            g = (g + 1) % nG;
            p = 0;
        }
        else if (p < 0)
        {
            g = (g + nG - 1) % nG;
            p = (int) gs[(size_t) g].presets.size() - 1;
        }
        loadPreset (g, p);
    }

    NebulaProcessor& proc;
    NebulaLook look;
    ListModel genreModel, presetModel;
    juce::ListBox genreList { "genres", nullptr };
    juce::ListBox presetList { "presets", nullptr };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, skyBtn { "SKY: AUTO" };
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::MidiKeyboardComponent keyboard;

    std::vector<Star> stars;
    std::vector<Cloud> clouds;
    std::vector<Mote> motes;
    std::vector<Ring> rings;
    std::vector<Ripple> ripples;
    std::vector<Spark> sparks;
    juce::Random rnd;
    std::vector<Bld> blds;
    int shownGenre = 0;
    float sceneFade = 1.f;

    bool  prevHeld[128] {};
    float beam[128] {};

    int curGenre = 0, loadedGenre = 0, loadedPreset = 0, skyMode = 0;
    juce::Colour curA { 0xff5ee7ff }, curB { 0xffff6ad5 }, tgtA { 0xff5ee7ff }, tgtB { 0xffff6ad5 };
    float day = 0.f, autoPhase = 0.f, t = 0.f, lv = 0.f, ringTimer = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaEditor)
};

juce::AudioProcessorEditor* NebulaProcessor::createEditor()
{
    nlog ("createEditor");
    auto* e = new NebulaEditor (*this);
    nlog ("createEditor done");
    return e;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NebulaProcessor(); }
