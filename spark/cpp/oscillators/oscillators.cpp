#include "daisysp.h"
#include "daisy_spark.h"
#include <cstdarg>
#include <cstdio>

using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

static Spark spark;
static SparkDiagnostics diagnostics(spark);
static SparkRuntime runtime(spark, diagnostics);

static Oscillator            osc;
static Oscillator            supersawLow;
static Oscillator            supersawHigh;
static VariableSawOscillator variableSaw;
static VariableShapeOscillator variableShape;
static VosimOscillator       vosim;
static FormantOscillator     formantOsc;
static ZOscillator           zOsc;
static Fm2                   fm2;
static HarmonicOscillator<8> harmonicOsc;
static StringVoice           stringVoice;
static Particle              particle;
static SyntheticBassDrum     synthBassDrum;
static GrainletOscillator    grainlet;
static Overdrive             overdrive;
static Parameter             freqParam;
static Parameter             shapeParam;

static const auto SAVE_DELAY_MS       = 3000U;
static float      sampleRate          = 48000.0f;
static float      drumPhase           = 0.0f;
static bool       encoderTurnedWhilePressed = false;
static uint32_t   lastEncoderPressMs        = 0;
static float      lastKnob2ShapeLog         = -1.0f;
static float      lastKnob1LogValue         = -1.0f;
static float      shapeTarget               = -1.0f;
static float      shapeSmoothed             = -1.0f;
static uint32_t   lastKnobFeedbackMs        = 0;
static Spark::LedCalibration kOnboardLedCalibration = {
    1.00f, // global_gain
    1.80f, // red_gain
    1.65f, // green_gain
    0.65f, // blue_gain
    1.20f  // gamma
};

// I2C RGB in front of PEL12T (or similar). Set in Makefile, e.g. -DSPARK_ENCODER_I2C_ADDR7=0x30
#ifndef SPARK_ENCODER_I2C_ADDR7
#define SPARK_ENCODER_I2C_ADDR7 0u
#endif
#ifndef SPARK_ENCODER_I2C_REG
#define SPARK_ENCODER_I2C_REG 0u
#endif

// Match onboard until you tune the shaft separately.
static const Spark::LedCalibration kEncoderI2cLedCalibration = kOnboardLedCalibration;

enum SparkModes {
    MODE_WAVEFORMS = 0,
    MODE_MACRO_A   = 1,
    MODE_MACRO_B   = 2,
    MODE_COUNT     = 3
};

enum Waveforms {
    WAVE_SIN             = 0,
    WAVE_POLYBLEP_TRI    = 1,
    WAVE_POLYBLEP_SAW    = 2,
    WAVE_POLYBLEP_SQUARE = 3,
    WAVE_RAMP            = 4,
    WAVE_SUPERSAW        = 5,
    WAVE_COUNT           = 6
};

enum MacrosBankA {
    MACRO_A_VARIABLE_SAW = 0,
    MACRO_A_VARIABLE_SHAPE = 1,
    MACRO_A_FM2            = 2,
    MACRO_A_FORMANT        = 3,
    MACRO_A_HARMONIC       = 4,
    MACRO_A_ZOSC           = 5,
    MACRO_A_COUNT          = 6
};

enum MacrosBankB {
    MACRO_B_VOSIM           = 0,
    MACRO_B_STRING          = 1,
    MACRO_B_PARTICLE        = 2,
    // Placeholder name kept for UI/backward compatibility.
    // Implemented with GrainletOscillator until a dedicated ring-mod noise block is added.
    MACRO_B_RING_MOD_NOISE = 3,
    MACRO_B_OVERDRIVE      = 4,
    MACRO_B_BASS_DRUM_CLICK = 5,
    MACRO_B_COUNT           = 6
};

struct SparkSettings {
    int   sparkMode;
    int   waveform;
    float waveformFreq;
    int   macroA;
    int   macroB;

    bool operator!=(const SparkSettings& a) const {
        return a.sparkMode != sparkMode ||
               a.waveform != waveform ||
               a.waveformFreq != waveformFreq ||
               a.macroA != macroA ||
               a.macroB != macroB;
    }
} storageState;

PersistentStorage<SparkSettings> storage(spark.seed.qspi);

#ifndef SPARK_DEBUG_ENABLE
#define SPARK_DEBUG_ENABLE 1
#endif

#ifndef SPARK_DEBUG_DEFAULT_LEVEL
#define SPARK_DEBUG_DEFAULT_LEVEL 2
#endif

#ifndef SPARK_DEBUG_DEFAULT_MASK
#define SPARK_DEBUG_DEFAULT_MASK 0x0F
#endif

#ifndef SPARK_DEBUG_STATUS_INTERVAL_MS
#define SPARK_DEBUG_STATUS_INTERVAL_MS 1000U
#endif

#ifndef SPARK_ENCODER_CLICK_WORKAROUND
#define SPARK_ENCODER_CLICK_WORKAROUND 1
#endif

#ifndef SPARK_TRACE_ENABLE
#define SPARK_TRACE_ENABLE 0
#endif

#ifndef SPARK_I2C_SCAN_BOOT
#define SPARK_I2C_SCAN_BOOT 1
#endif
// TEMP WORKAROUND (remove after hardware fix):
// Current Spark hardware revision has an encoder-click electrical issue, so we temporarily
// use SW2-held + encoder-turn for bank selection.
// To remove this workaround after hardware fix:
//   1) set SPARK_ENCODER_CLICK_WORKAROUND to 0
//   2) bank selection reverts to encoder-click + turn (with grace window)
// Encoder click action code (click-to-cycle-bank) remains in place below.

static constexpr float kFreqMinHz          = 32.0f;
static constexpr float kFreqMaxHz          = 650.0f;
static constexpr uint32_t kBankSwitchGraceMs = 180;
static constexpr uint32_t kKnobFeedbackIntervalMs = 120;
static constexpr uint32_t kSw1HoldMs = 220;
static constexpr uint32_t kSw2HoldMs = 220;
static constexpr float kShapeDeadband = 0.005f;
static constexpr float kShapeSmoothingAlpha = 0.20f;
static constexpr float kShapeLogStep = 0.02f;
// k1: raw ADC wiggles ~0.1–1% of range; that maps to noticeable Hz. Smooth for freq; freeze while
// k1 is latched after sw2.
static constexpr float kPitchSmoothAlpha  = 0.16f;
static constexpr float kFreqUpdateMinDeltaHz = 0.5f;
static constexpr float kKnob1Min = 0.00f;
static constexpr float kKnob1Max = 0.96f;
static constexpr float kKnob2Min = 0.00f;
static constexpr float kKnob2Max = 0.96f;
static constexpr float kWaveOutputGain = 0.68f;
static constexpr float kWaveGainSine = 1.0000f;
static constexpr float kWaveGainTri = 1.0307f;      // 4.37 / 4.24
static constexpr float kWaveGainSaw = 1.0069f;      // 4.37 / 4.34
static constexpr float kWaveGainSquare = 1.3571f;   // 4.37 / 3.22
static constexpr float kWaveGainRamp = 0.9541f;     // 4.37 / 4.58
static constexpr float kWaveGainSuperSaw = 0.9440f; // reduced to tame beat peaks (4.87V -> ~4.37V target)
// Onboard LED response tuning (single middle-brightness profile, warm motion).
static constexpr float kLedLevelMin         = 0.22f;
static constexpr float kLedLevelMax         = 1.00f;
static constexpr float kLedLevelGamma       = 1.35f;
static constexpr float kLedSmoothingAlpha   = 0.08f;
static constexpr float kLedMaxStepPerUpdate = 0.010f;
static constexpr float kLedModifierSmoothingAlpha   = 0.35f;
static constexpr float kLedModifierMaxStepPerUpdate = 0.08f;
static constexpr float kStartupWhiteLevel   = 0.75f;
static constexpr uint32_t kStartupWhiteHoldMs = 2000;
static constexpr float kStartupWhiteRScale = 0.85f;
static constexpr float kStartupWhiteGScale = 0.72f;
static constexpr float kStartupWhiteBScale = 1.45f;
static float            modifierHarmonics = 0.0f;
static float            modifierMorph = 0.0f;
static uint32_t         sw1PressStartMs = 0;
static bool             sw1ModifierActive = false;
static uint32_t         sw2PressStartMs = 0;
static bool             sw2ModifierActive = false;
// While sw1 modifier is on, k1 edits harmonics (k3) and does not update pitch; on release, do not
// snap freq to k1 until the knob moves again (otherwise k3 and k1 appear coupled).
static bool             prevSw1ModifierForLatch = false;
// While sw2 modifier is on, k2 edits morph (k4) and does not update timbre; on release, do not
// snap timbre to k2 until the knob moves again (same coupling prevention as k1/k3).
static bool             prevSw2ModifierForLatch = false;
static bool             k1PitchFrozenAfterModifier = false;
static float            k1NormLatchOnModifierExit = 0.0f;
static bool             k2ShapeFrozenAfterModifier = false;
static float            k2NormLatchOnModifierExit = 0.0f;
static constexpr float  kKnobRearmMove   = 0.035f;
static float            k1PitchForFreq  = -1.0f;
static float            k1LedLevelSmooth = -1.0f;
static float            k2LedLevelSmooth = -1.0f;
static float            k3LedLevelSmooth = -1.0f;
static float            k4LedLevelSmooth = -1.0f;
static bool             startupWhiteActive = true;
static uint32_t         startupWhiteStartMs = 0;

static float CalibrateKnob(float raw, float minv, float maxv)
{
    if(maxv <= minv)
    {
        return 0.0f;
    }
    float v = (raw - minv) / (maxv - minv);
    if(v < 0.0f)
    {
        v = 0.0f;
    }
    if(v > 1.0f)
    {
        v = 1.0f;
    }
    return v;
}

static float Knob1Calibrated()
{
    return CalibrateKnob(spark.knob1.Value(), kKnob1Min, kKnob1Max);
}

static float Knob2Calibrated()
{
    return CalibrateKnob(spark.knob2.Value(), kKnob2Min, kKnob2Max);
}

static float CurrentShapeValue()
{
    if(shapeSmoothed < 0.0f)
    {
        return Knob2Calibrated();
    }
    return shapeSmoothed;
}

static float CurrentPitchFrequency(float pitchNorm)
{
    const float ratio = kFreqMaxHz / kFreqMinHz;
    return kFreqMinHz * powf(ratio, fclamp(pitchNorm, 0.0f, 1.0f));
}

static float FrequencyToPitchNorm(float freqHz)
{
    const float ratio = kFreqMaxHz / kFreqMinHz;
    if(ratio <= 1.0f || freqHz <= 0.0f)
    {
        return 0.0f;
    }
    const float norm = logf(freqHz / kFreqMinHz) / logf(ratio);
    return fclamp(norm, 0.0f, 1.0f);
}

static float ApplyFinalLimiter(float input)
{
    return input;
}

template <size_t N>
static const char* NameFromTable(const char* const (&table)[N], int index, const char* fallback)
{
    return (index >= 0 && index < static_cast<int>(N)) ? table[index] : fallback;
}

static float CurrentModelOutputGain(const SparkSettings& current)
{
    if(current.sparkMode == MODE_WAVEFORMS)
    {
        switch(current.waveform)
        {
            case WAVE_SIN: return kWaveOutputGain * kWaveGainSine;
            case WAVE_POLYBLEP_TRI: return kWaveOutputGain * kWaveGainTri;
            case WAVE_POLYBLEP_SAW: return kWaveOutputGain * kWaveGainSaw;
            case WAVE_POLYBLEP_SQUARE: return kWaveOutputGain * kWaveGainSquare;
            case WAVE_RAMP: return kWaveOutputGain * kWaveGainRamp;
            case WAVE_SUPERSAW: return kWaveOutputGain * kWaveGainSuperSaw;
            default: return kWaveOutputGain;
        }
    }
    if(current.sparkMode == MODE_MACRO_A)
    {
        switch(current.macroA)
        {
            case MACRO_A_VARIABLE_SAW: return 0.86f;
            case MACRO_A_VARIABLE_SHAPE: return 0.86f;
            case MACRO_A_FM2: return 0.72f;
            case MACRO_A_FORMANT: return 0.82f;
            case MACRO_A_HARMONIC: return 0.74f;
            case MACRO_A_ZOSC: return 0.80f;
            default: return 0.80f;
        }
    }
    switch(current.macroB)
    {
        case MACRO_B_VOSIM: return 0.76f;
        case MACRO_B_STRING: return 0.80f;
        case MACRO_B_PARTICLE: return 0.72f;
        case MACRO_B_RING_MOD_NOISE: return 0.72f;
        case MACRO_B_OVERDRIVE: return 0.66f;
        case MACRO_B_BASS_DRUM_CLICK: return 0.80f;
        default: return 0.76f;
    }
}

struct ModelControls
{
    float timbre;
    float harmonics;
    float morph;
};

static ModelControls CurvedControls(const SparkSettings& current,
                                    float                timbre,
                                    float                harmonics,
                                    float                morph)
{
    ModelControls out = {timbre, harmonics, morph};

    if(current.sparkMode == MODE_WAVEFORMS)
    {
        out.timbre = powf(timbre, 0.85f);
        return out;
    }

    if(current.sparkMode == MODE_MACRO_A)
    {
        switch(current.macroA)
        {
            case MACRO_A_FM2:
                out.timbre    = powf(timbre, 1.40f);
                out.harmonics = powf(harmonics, 0.80f);
                out.morph     = powf(morph, 1.25f);
                break;
            case MACRO_A_HARMONIC:
                out.timbre    = (timbre * 2.0f) - 1.0f;
                out.harmonics = powf(harmonics, 0.90f);
                out.morph     = powf(morph, 1.35f);
                break;
            case MACRO_A_FORMANT:
            case MACRO_A_ZOSC:
                out.harmonics = powf(harmonics, 0.75f);
                out.morph     = powf(morph, 1.10f);
                break;
            default: break;
        }
        return out;
    }

    switch(current.macroB)
    {
        case MACRO_B_OVERDRIVE:
            out.timbre    = powf(timbre, 1.35f);
            out.harmonics = powf(harmonics, 0.88f);
            break;
        case MACRO_B_PARTICLE:
            out.harmonics = powf(harmonics, 0.90f);
            out.morph     = powf(morph, 1.05f);
            break;
        case MACRO_B_RING_MOD_NOISE:
            out.harmonics = powf(harmonics, 0.85f);
            out.morph     = powf(morph, 1.10f);
            break;
        case MACRO_B_BASS_DRUM_CLICK:
            out.timbre    = powf(timbre, 0.85f);
            out.harmonics = powf(harmonics, 0.90f);
            out.morph     = powf(morph, 1.12f);
            break;
        default: break;
    }
    return out;
}

static const char* const kWaveModelNames[WAVE_COUNT] = {
    "Sine",
    "Tri",
    "Saw",
    "Square",
    "Ramp",
    "SuperSaw",
};

static const char* const kMacroAModelNames[MACRO_A_COUNT] = {
    "VarSaw",
    "VarShape",
    "FM2",
    "Formant",
    "Harmonic",
    "ZOsc",
};

static const char* const kMacroBModelNames[MACRO_B_COUNT] = {
    "Vosim",
    "String",
    "Particle",
    "Grainlet",
    "Drive",
    "Kick",
};

static const char* ModeName(int mode)
{
    switch(mode)
    {
        case MODE_WAVEFORMS: return "wave";
        case MODE_MACRO_A: return "macroA";
        case MODE_MACRO_B: return "macroB";
        default: return "unknown";
    }
}

static const char* BankDisplayName(int mode)
{
    switch(mode)
    {
        case MODE_WAVEFORMS: return "Wave";
        case MODE_MACRO_A: return "MacroA";
        case MODE_MACRO_B: return "MacroB";
        default: return "Unknown";
    }
}

static const char* WaveName(int waveform)
{
    return NameFromTable(kWaveModelNames, waveform, "Wave?");
}

static const char* MacroAName(int macroA)
{
    return NameFromTable(kMacroAModelNames, macroA, "MacroA?");
}

static const char* MacroBName(int macroB)
{
    return NameFromTable(kMacroBModelNames, macroB, "MacroB?");
}

static const char* CurrentModelName(const SparkSettings& current)
{
    if(current.sparkMode == MODE_WAVEFORMS)
    {
        return WaveName(current.waveform);
    }
    if(current.sparkMode == MODE_MACRO_A)
    {
        return MacroAName(current.macroA);
    }
    return MacroBName(current.macroB);
}

static int ParamPct(float value);

static void RenderFriendlyStatusLine(const SparkSettings& current)
{
    if(DBG_INFO > diagnostics.Level() || (diagnostics.Mask() & DBG_CAT_STATE) == 0)
    {
        return;
    }
    char line[192];
    snprintf(line,
             sizeof(line),
             "%s %s freq=%dHz timbre=%d%% harmonics=%d%% morph=%d%%",
             BankDisplayName(current.sparkMode),
             CurrentModelName(current),
             static_cast<int>(current.waveformFreq),
             static_cast<int>(Knob2Calibrated() * 100.0f),
             ParamPct(modifierHarmonics),
             ParamPct(modifierMorph));
    spark.seed.Print("\r%-72s", line);
}

struct RgbColor
{
    float r;
    float g;
    float b;
};

// ROYGBIV palette with red intentionally moved to the end:
// orange, yellow, green, blue, indigo, violet, red
static constexpr RgbColor kRoygbivRedLast[] = {
    {1.00f, 0.35f, 0.00f}, // orange
    {0.90f, 0.80f, 0.00f}, // yellow
    {0.00f, 0.90f, 0.00f}, // green
    {0.00f, 0.25f, 1.00f}, // blue
    {0.20f, 0.00f, 0.70f}, // indigo
    {0.55f, 0.00f, 0.75f}, // violet
    {0.90f, 0.00f, 0.00f}, // red (last)
};
static constexpr int kLedPaletteCount
    = static_cast<int>(sizeof(kRoygbivRedLast) / sizeof(kRoygbivRedLast[0]));

static RgbColor PaletteColor(int index)
{
    return kRoygbivRedLast[WrapIndex(index, kLedPaletteCount)];
}

static float WarmLedLevel(float sourceNorm)
{
    const float x = fclamp(sourceNorm, 0.0f, 1.0f);
    const float shaped = powf(x, kLedLevelGamma);
    return kLedLevelMin + ((kLedLevelMax - kLedLevelMin) * shaped);
}

static float SmoothLedLevel(float target, float prev)
{
    if(prev < 0.0f)
    {
        return target;
    }
    const float filtered = prev + (kLedSmoothingAlpha * (target - prev));
    const float delta = filtered - prev;
    const float limited = fclamp(delta, -kLedMaxStepPerUpdate, kLedMaxStepPerUpdate);
    return prev + limited;
}

static float SmoothLedLevelFast(float target, float prev)
{
    if(prev < 0.0f)
    {
        return target;
    }
    const float filtered = prev + (kLedModifierSmoothingAlpha * (target - prev));
    const float delta = filtered - prev;
    const float limited = fclamp(delta,
                                 -kLedModifierMaxStepPerUpdate,
                                 kLedModifierMaxStepPerUpdate);
    return prev + limited;
}

static RgbColor StartupWhiteRgb()
{
    RgbColor c;
    c.r = fclamp(kStartupWhiteLevel * kStartupWhiteRScale, 0.0f, 1.0f);
    c.g = fclamp(kStartupWhiteLevel * kStartupWhiteGScale, 0.0f, 1.0f);
    c.b = fclamp(kStartupWhiteLevel * kStartupWhiteBScale, 0.0f, 1.0f);
    return c;
}

static void SetStartupWhiteLeds()
{
    const RgbColor startupWhite = StartupWhiteRgb();

    float led1r, led1g, led1b;
    spark.ApplyLedCalibration(Spark::LedTarget::Onboard,
                              startupWhite.r,
                              startupWhite.g,
                              startupWhite.b,
                              led1r,
                              led1g,
                              led1b);

    float led2r, led2g, led2b;
    spark.ApplyLedCalibration(Spark::LedTarget::Onboard,
                              startupWhite.r,
                              startupWhite.g,
                              startupWhite.b,
                              led2r,
                              led2g,
                              led2b);

    spark.led1.Set(led1r, led1g, led1b);
    spark.led2.Set(led2r, led2g, led2b);
    spark.SetEncoderI2cRgb(startupWhite.r, startupWhite.g, startupWhite.b);
    spark.UpdateLeds();
}

static void DebugStatusNow(const char* reason)
{
    SparkSettings& current = storage.GetSettings();
    (void)reason;
    RenderFriendlyStatusLine(current);
}

static void LogKnobFeedback(const char* source)
{
    const uint32_t now = System::GetNow();
    if((now - lastKnobFeedbackMs) < kKnobFeedbackIntervalMs)
    {
        return;
    }
    lastKnobFeedbackMs = now;

    char label[16];
    snprintf(label, sizeof(label), "%-10s", source);

    SparkSettings& current = storage.GetSettings();
    const bool     isK1Event = (source[1] == '1');
    const int      freqHz    = static_cast<int>(current.waveformFreq);
    const int      timbrePct = static_cast<int>(Knob2Calibrated() * 100.0f);
    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    isK1Event ? "%sfreq -> %dHz" : "%stimbre -> %d%%",
                    label,
                    isK1Event ? freqHz : timbrePct);
}

static int ParamPct(float value)
{
    if(value < 0.0f)
    {
        value = 0.0f;
    }
    if(value > 1.0f)
    {
        value = 1.0f;
    }
    return static_cast<int>(value * 100.0f);
}

static void LogModifierFeedback(const char* source)
{
    const uint32_t now = System::GetNow();
    if((now - lastKnobFeedbackMs) < kKnobFeedbackIntervalMs)
    {
        return;
    }
    lastKnobFeedbackMs = now;

    char label[16];
    snprintf(label, sizeof(label), "%-10s", source);

    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    (source[1] == '3') ? "%sharmonics -> %d%%" : "%smorph -> %d%%",
                    label,
                    (source[1] == '3') ? ParamPct(modifierHarmonics) : ParamPct(modifierMorph));
}

static void LogModifierState(const char* source, const char* state)
{
    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    "%-10smod -> %s",
                    source,
                    state);
}

static void DebugLog(uint8_t level, uint8_t category, const char* format, ...)
{
#if SPARK_DEBUG_ENABLE
    va_list args;
    va_start(args, format);
    char line[192];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    diagnostics.Log(level, category, "%s", line);
#else
    (void)level;
    (void)category;
    (void)format;
#endif
}

#if SPARK_TRACE_ENABLE
#define TRACE_CTRL_LOG(...) DebugLog(DBG_TRACE, DBG_CAT_CTRL, __VA_ARGS__)
#else
#define TRACE_CTRL_LOG(...) \
    do                     \
    {                      \
    } while(0)
#endif

static void DebugInit()
{
#if SPARK_DEBUG_ENABLE
    diagnostics.Init(SPARK_DEBUG_DEFAULT_LEVEL, SPARK_DEBUG_DEFAULT_MASK, "oscillators");
    DebugLog(DBG_INFO,
             DBG_CAT_STATE,
             "spark debug on: level=%d mask=0x%02x",
             diagnostics.Level(),
             diagnostics.Mask());
#if SPARK_ENCODER_CLICK_WORKAROUND
    DebugLog(DBG_INFO,
             DBG_CAT_STATE,
             "bank select: hold sw2 while turning encoder");
#endif

    // I2C scan is printed later (after USB CDC attach) from the main loop.
#endif
}

static void DebugMaybePrintI2cScan()
{
#if SPARK_DEBUG_ENABLE && (SPARK_I2C_SCAN_BOOT)
    static bool i2cScanPrinted = false;
    if(i2cScanPrinted || System::GetNow() < 2000U)
    {
        return;
    }

    i2cScanPrinted = true;
    spark.seed.PrintLine("");
    spark.seed.PrintLine("---- delayed i2c scan ----");

    if(!spark.InitPeripheralI2c())
    {
        spark.seed.PrintLine("i2c scan: peripheral I2C init failed");
        spark.seed.PrintLine("--------------------------");
        return;
    }

    uint8_t found[16];
    const uint8_t count = spark.ScanI2cDevices(found, 16);
    if(count == 0)
    {
        spark.seed.PrintLine("i2c scan: no devices found on peripheral bus");
    }
    else
    {
        spark.seed.PrintLine("i2c scan: %d device(s) found", count);
        for(uint8_t i = 0; i < count; i++)
        {
            spark.seed.PrintLine("i2c addr: 0x%02x", found[i]);
        }
    }
    spark.seed.PrintLine("--------------------------");
#endif
}

static void DebugMaybeStatus()
{
    if(!diagnostics.StatusDue(SPARK_DEBUG_STATUS_INTERVAL_MS))
    {
        return;
    }

    SparkSettings& current = storage.GetSettings();
    RenderFriendlyStatusLine(current);
}

static void MarkInteraction()
{
    runtime.MarkInteraction();
    TRACE_CTRL_LOG("interaction marked");
}

static void UpdateLeds()
{
    if(startupWhiteActive)
    {
        SetStartupWhiteLeds();
        return;
    }

    SparkSettings& current = storage.GetSettings();
    const int itemColorIndex = (current.sparkMode == MODE_WAVEFORMS)
                                   ? current.waveform
                               : (current.sparkMode == MODE_MACRO_A) ? current.macroA
                                                                       : current.macroB;

    const RgbColor itemColor = PaletteColor(itemColorIndex);

    // Maintain independent brightness states for k1/k2/k3/k4.
    const float k1Target = WarmLedLevel(FrequencyToPitchNorm(current.waveformFreq));
    const float k2Target = WarmLedLevel(CurrentShapeValue());
    const float k3Target = WarmLedLevel(modifierHarmonics);
    const float k4Target = WarmLedLevel(modifierMorph);

    k1LedLevelSmooth = SmoothLedLevel(k1Target, k1LedLevelSmooth);
    k2LedLevelSmooth = SmoothLedLevel(k2Target, k2LedLevelSmooth);
    k3LedLevelSmooth = SmoothLedLevelFast(k3Target, k3LedLevelSmooth);
    k4LedLevelSmooth = SmoothLedLevelFast(k4Target, k4LedLevelSmooth);

    // Display k1/k2 by default; while holding modifiers show k3/k4 respectively.
    const float led1Level = sw1ModifierActive ? k3LedLevelSmooth : k1LedLevelSmooth;
    const float led2Level = sw2ModifierActive ? k4LedLevelSmooth : k2LedLevelSmooth;

    // Preserve hue across brightness levels: calibrate color once, then scale intensity per LED.
    float baseR, baseG, baseB;
    spark.ApplyLedCalibration(
        Spark::LedTarget::Onboard, itemColor.r, itemColor.g, itemColor.b, baseR, baseG, baseB);

    const float led1r = fclamp(baseR * led1Level, 0.0f, 1.0f);
    const float led1g = fclamp(baseG * led1Level, 0.0f, 1.0f);
    const float led1b = fclamp(baseB * led1Level, 0.0f, 1.0f);

    const float led2r = fclamp(baseR * led2Level, 0.0f, 1.0f);
    const float led2g = fclamp(baseG * led2Level, 0.0f, 1.0f);
    const float led2b = fclamp(baseB * led2Level, 0.0f, 1.0f);

    spark.led1.Set(led1r, led1g, led1b);
    spark.led2.Set(led2r, led2g, led2b);
    const RgbColor startupWhite = StartupWhiteRgb();
    spark.SetEncoderI2cRgb(startupWhite.r, startupWhite.g, startupWhite.b);
    spark.UpdateLeds();
}

void ProcessEncoder()
{
    SparkSettings& current = storage.GetSettings();
    const int32_t  inc     = -spark.encoder.Increment();
    const uint32_t nowMs   = System::GetNow();

    if(spark.encoder.RisingEdge())
    {
        encoderTurnedWhilePressed = false;
        lastEncoderPressMs        = nowMs;
        diagnostics.Log(DBG_INFO, DBG_CAT_CTRL, "%-10sstate -> down", "enc click");
        TRACE_CTRL_LOG("encoder press");
        DebugStatusNow("enc-press");
    }

    if(spark.encoder.FallingEdge())
    {
        diagnostics.Log(DBG_INFO, DBG_CAT_CTRL, "%-10sstate -> up", "enc click");
        TRACE_CTRL_LOG("encoder release");
        DebugStatusNow("enc-release");
    }

    if(inc != 0)
    {
        const char* encDir = (inc > 0) ? "enc up" : "enc down";
        char        encLabel[16];
        snprintf(encLabel, sizeof(encLabel), "%-10s", encDir);
        bool bankSelectActive = false;
#if SPARK_ENCODER_CLICK_WORKAROUND
        // TEMP: use SW2 as bank-select modifier while encoder click hardware is broken.
        bankSelectActive = spark.button2.Pressed();
#else
        // Normal behavior: encoder click + turn selects bank.
        bankSelectActive
            = spark.encoder.Pressed() || ((nowMs - lastEncoderPressMs) <= kBankSwitchGraceMs);
#endif

        TRACE_CTRL_LOG("encoder turn inc=%ld enc=%d sw2=%d bank=%d",
                       static_cast<long>(inc),
                       spark.encoder.Pressed() ? 1 : 0,
                       spark.button2.Pressed() ? 1 : 0,
                       bankSelectActive ? 1 : 0);
        encoderTurnedWhilePressed = true;

        if(bankSelectActive)
        {
            current.sparkMode = WrapIndex(current.sparkMode + static_cast<int>(inc), MODE_COUNT);
            diagnostics.Log(DBG_INFO,
                            DBG_CAT_CTRL,
                            "%smode -> %s",
                            encLabel,
                            ModeName(current.sparkMode));
            DebugStatusNow("bank");
        }
        else if(current.sparkMode == MODE_WAVEFORMS)
        {
            current.waveform = WrapIndex(current.waveform + static_cast<int>(inc), WAVE_COUNT);
            diagnostics.Log(DBG_INFO,
                            DBG_CAT_CTRL,
                            "%swave -> %d (%s)",
                            encLabel,
                            current.waveform,
                            WaveName(current.waveform));
            DebugStatusNow("wave");
        }
        else if(current.sparkMode == MODE_MACRO_A)
        {
            current.macroA = WrapIndex(current.macroA + static_cast<int>(inc), MACRO_A_COUNT);
            diagnostics.Log(DBG_INFO,
                            DBG_CAT_CTRL,
                            "%smacroA -> %d (%s)",
                            encLabel,
                            current.macroA,
                            MacroAName(current.macroA));
            DebugStatusNow("macroA");
        }
        else
        {
            current.macroB = WrapIndex(current.macroB + static_cast<int>(inc), MACRO_B_COUNT);
            diagnostics.Log(DBG_INFO,
                            DBG_CAT_CTRL,
                            "%smacroB -> %d (%s)",
                            encLabel,
                            current.macroB,
                            MacroBName(current.macroB));
            DebugStatusNow("macroB");
        }
        MarkInteraction();
    }

    // Click action retained: click/release without turning cycles bank.
    // This should function once encoder-click hardware is corrected.
    bool clickEvent = spark.encoder.FallingEdge() && !encoderTurnedWhilePressed;
    if(clickEvent)
    {
        current.sparkMode = WrapIndex(current.sparkMode + 1, MODE_COUNT);
        diagnostics.Log(DBG_INFO, DBG_CAT_CTRL, "%-10smode -> %s", "enc click", ModeName(current.sparkMode));
        MarkInteraction();
        DebugStatusNow("enc-click-bank");
    }
}

void ProcessKnobs()
{
    SparkSettings& current = storage.GetSettings();
    const float shapeRaw = Knob2Calibrated();
    const float pitchNorm = Knob1Calibrated();

    if(k1PitchForFreq < 0.0f)
    {
        k1PitchForFreq = pitchNorm;
    }

    const bool sw1ModNow = sw1ModifierActive;
    if(!sw1ModNow && prevSw1ModifierForLatch)
    {
        k1PitchFrozenAfterModifier = true;
        k1NormLatchOnModifierExit  = pitchNorm;
        lastKnob1LogValue          = pitchNorm;
    }
    prevSw1ModifierForLatch = sw1ModNow;

    const bool sw2ModNow = sw2ModifierActive;
    if(!sw2ModNow && prevSw2ModifierForLatch)
    {
        // k2 was driving morph (k4); do not snap timbre to the knob — freeze timbre from k2
        // until the knob moves again (same idea as k1/k3).
        k2ShapeFrozenAfterModifier = true;
        k2NormLatchOnModifierExit  = shapeRaw;
    }
    prevSw2ModifierForLatch = sw2ModNow;

    bool modifierChanged = false;
    if(sw1ModifierActive)
    {
        const bool k3Up = (modifierHarmonics <= 0.0f) ? true : (pitchNorm >= modifierHarmonics);
        if(fabsf(pitchNorm - modifierHarmonics) > 0.01f)
        {
            modifierHarmonics = pitchNorm;
            modifierChanged   = true;
            LogModifierFeedback(k3Up ? "k3  up" : "k3  down");
        }
    }
    if(sw2ModifierActive)
    {
        const bool k4Up = (modifierMorph <= 0.0f) ? true : (shapeRaw >= modifierMorph);
        if(fabsf(shapeRaw - modifierMorph) > 0.01f)
        {
            modifierMorph = shapeRaw;
            modifierChanged = true;
            LogModifierFeedback(k4Up ? "k4  up" : "k4  down");
        }
    }
    if(modifierChanged)
    {
        MarkInteraction();
        DebugStatusNow((sw1ModifierActive && sw2ModifierActive) ? "sw1+sw2-mod"
                                                                : (sw1ModifierActive ? "sw1-mod"
                                                                                     : "sw2-mod"));
    }

    if(!sw1ModifierActive)
    {
        if(k1PitchFrozenAfterModifier)
        {
            if(fabsf(pitchNorm - k1NormLatchOnModifierExit) > kKnobRearmMove)
            {
                k1PitchFrozenAfterModifier = false;
                k1PitchForFreq             = pitchNorm;
            }
            else
            {
                lastKnob1LogValue = pitchNorm;
            }
        }
        else
        {
            k1PitchForFreq += kPitchSmoothAlpha * (pitchNorm - k1PitchForFreq);
        }

        // Musical pitch mapping centered at middle C; use k1PitchForFreq (low-pass) not raw ADC.
        if(!k1PitchFrozenAfterModifier)
        {
            const float newFreq = CurrentPitchFrequency(k1PitchForFreq);
            if(fabsf(newFreq - current.waveformFreq) > kFreqUpdateMinDeltaHz)
            {
                const bool k1Up
                    = (lastKnob1LogValue < 0.0f) ? true : (k1PitchForFreq >= lastKnob1LogValue);
                current.waveformFreq = newFreq;
                MarkInteraction();
                TRACE_CTRL_LOG("freq -> %.2f", current.waveformFreq);
                LogKnobFeedback(k1Up ? "k1  up" : "k1  down");
                lastKnob1LogValue = k1PitchForFreq;
                DebugStatusNow("freq");
            }
            else
            {
                lastKnob1LogValue = k1PitchForFreq;
            }
        }
    }

    if(sw2ModifierActive)
    {
        return;
    }

    (void)shapeParam.Process();
    if(k2ShapeFrozenAfterModifier)
    {
        if(fabsf(shapeRaw - k2NormLatchOnModifierExit) > kKnobRearmMove)
        {
            k2ShapeFrozenAfterModifier = false;
        }
    }

    if(!k2ShapeFrozenAfterModifier)
    {
        if(shapeTarget < 0.0f || shapeSmoothed < 0.0f)
        {
            shapeTarget   = shapeRaw;
            shapeSmoothed = shapeRaw;
        }

        if(fabsf(shapeRaw - shapeTarget) > kShapeDeadband)
        {
            shapeTarget = shapeRaw;
        }

        shapeSmoothed += kShapeSmoothingAlpha * (shapeTarget - shapeSmoothed);
        const float shapeNow = shapeSmoothed;
        if(lastKnob2ShapeLog < 0.0f || fabsf(shapeNow - lastKnob2ShapeLog) > kShapeLogStep)
        {
            const bool k2Up = (lastKnob2ShapeLog < 0.0f) ? true : (shapeNow >= lastKnob2ShapeLog);
            lastKnob2ShapeLog = shapeNow;
            MarkInteraction();
            TRACE_CTRL_LOG("shape -> %.3f", shapeNow);
            LogKnobFeedback(k2Up ? "k2  up" : "k2  down");
            DebugStatusNow("shape");
        }
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    SparkSettings& current = storage.GetSettings();
    const float    timbreIn = CurrentShapeValue();
    const float    p3       = modifierHarmonics;
    const float    p4       = modifierMorph;
    const ModelControls controls = CurvedControls(current, timbreIn, p3, p4);
    const float         timbre   = controls.timbre;
    const float         harmonics = controls.harmonics;
    const float         morph     = controls.morph;

    osc.SetFreq(current.waveformFreq);

    for(size_t i = 0; i < size; i++)
    {
        float sig = 0.0f;

        if(current.sparkMode == MODE_WAVEFORMS)
        {
            switch(current.waveform)
            {
                case WAVE_SIN:
                    // Sine-to-triangle tilt for useful low-harmonic shaping.
                    osc.SetWaveform(Oscillator::WAVE_SIN);
                    sig = osc.Process();
                    break;
                case WAVE_POLYBLEP_TRI:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
                    osc.SetPw(0.5f + ((timbre - 0.5f) * 0.3f));
                    sig = osc.Process();
                    break;
                case WAVE_POLYBLEP_SAW:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                    osc.SetPw(0.3f + (timbre * 0.4f));
                    sig = osc.Process();
                    break;
                case WAVE_RAMP:
                    osc.SetWaveform(Oscillator::WAVE_RAMP);
                    osc.SetPw(0.3f + (timbre * 0.4f));
                    sig = osc.Process();
                    break;
                case WAVE_SUPERSAW:
                {
                    const float detune = 0.002f + (harmonics * 0.035f);
                    const float spread = 0.55f + (morph * 0.45f);
                    const float f0 = current.waveformFreq;
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                    supersawLow.SetFreq(f0 * (1.0f - detune));
                    supersawHigh.SetFreq(f0 * (1.0f + detune));
                    const float center = osc.Process();
                    const float low = supersawLow.Process();
                    const float high = supersawHigh.Process();
                    const float mix = (center * (1.0f - (spread * 0.35f)))
                                      + ((low + high) * (0.5f * spread * 0.90f));
                    sig = mix;
                    break;
                }
                case WAVE_POLYBLEP_SQUARE:
                    // Band-limited square; PW uses linear shape (not CurvedControls timbre pow).
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
                    {
                        const float pw = 0.10f + fclamp(timbreIn, 0.0f, 1.0f) * 0.80f;
                        osc.SetPw(pw);
                    }
                    sig = osc.Process();
                    break;
                default:
                    osc.SetWaveform(Oscillator::WAVE_SIN);
                    sig = osc.Process();
                    break;
            }
            // Neutral waveform path: no extra color stage.
        }
        else if(current.sparkMode == MODE_MACRO_A)
        {
            switch(current.macroA)
            {
                case MACRO_A_VARIABLE_SAW:
                    variableSaw.SetFreq(current.waveformFreq);
                    variableSaw.SetPW(((timbre * 2.0f) - 1.0f) * (0.4f + (harmonics * 0.6f)));
                    variableSaw.SetWaveshape((timbre * (1.0f - (morph * 0.5f))) + (morph * 0.5f));
                    sig = variableSaw.Process();
                    break;

                case MACRO_A_FM2:
                    fm2.SetFrequency(current.waveformFreq);
                    fm2.SetRatio(0.25f + (harmonics * 7.75f));
                    fm2.SetIndex(0.05f + (timbre * (0.95f + (morph * 8.0f))));
                    sig = fm2.Process();
                    break;

                case MACRO_A_FORMANT:
                    formantOsc.SetCarrierFreq(current.waveformFreq);
                    formantOsc.SetFormantFreq(current.waveformFreq * (1.0f + (timbre * (2.0f + (harmonics * 6.0f)))));
                    formantOsc.SetPhaseShift((morph * 2.0f) - 1.0f);
                    sig = formantOsc.Process();
                    break;

                case MACRO_A_ZOSC:
                    zOsc.SetFreq(current.waveformFreq);
                    zOsc.SetFormantFreq(current.waveformFreq * (1.0f + (timbre * (2.0f + (harmonics * 6.0f)))));
                    zOsc.SetShape((timbre * 0.7f) + (morph * 0.3f));
                    zOsc.SetMode((morph * 2.0f) - 1.0f);
                    sig = zOsc.Process();
                    break;

                case MACRO_A_VARIABLE_SHAPE:
                    variableShape.SetFreq(current.waveformFreq);
                    variableShape.SetPW(0.02f + (timbre * 0.96f));
                    variableShape.SetWaveshape((timbre * 0.65f) + (harmonics * 0.35f));
                    variableShape.SetSync(true);
                    variableShape.SetSyncFreq(current.waveformFreq * (0.25f + (morph * 7.75f)));
                    sig = variableShape.Process();
                    break;

                case MACRO_A_HARMONIC:
                {
                    harmonicOsc.SetFreq(current.waveformFreq);
                    harmonicOsc.SetFirstHarmIdx(1 + static_cast<int>(harmonics * 6.0f));
                    float amps[8];
                    const float tilt = timbre;
                    float sum = 0.0f;
                    for(int h = 0; h < 8; ++h)
                    {
                        const float rank = static_cast<float>(h + 1);
                        float       a    = powf(rank, -(1.0f + (morph * 2.0f)));
                        a *= (tilt >= 0.0f) ? powf(rank, -tilt) : powf(rank, fabsf(tilt));
                        amps[h] = a;
                        sum += a;
                    }
                    const float norm = (sum > 0.0f) ? (0.9f / sum) : 1.0f;
                    for(int h = 0; h < 8; ++h)
                    {
                        amps[h] *= norm;
                    }
                    harmonicOsc.SetAmplitudes(amps);
                    sig = harmonicOsc.Process();
                    break;
                }

                default:
                    sig = 0.0f;
                    break;
            }
        }
        else
        {
            switch(current.macroB)
            {
                case MACRO_B_VOSIM:
                    vosim.SetFreq(current.waveformFreq);
                    vosim.SetForm1Freq(current.waveformFreq * (1.0f + (timbre * (1.0f + (harmonics * 3.0f)))));
                    vosim.SetForm2Freq(current.waveformFreq * (2.0f + (timbre * (2.0f + (morph * 6.0f)))));
                    vosim.SetShape(((timbre * 2.0f) - 1.0f) * (0.35f + (harmonics * 0.65f)));
                    sig = vosim.Process();
                    break;

                case MACRO_B_STRING:
                    stringVoice.SetSustain(true);
                    stringVoice.SetFreq(current.waveformFreq);
                    stringVoice.SetAccent(0.35f + (harmonics * 0.65f));
                    stringVoice.SetStructure((timbre * 0.7f) + (harmonics * 0.3f));
                    stringVoice.SetBrightness((timbre * 0.6f) + (morph * 0.4f));
                    stringVoice.SetDamping(1.0f - (((timbre * 0.65f) + (morph * 0.35f)) * 0.8f));
                    sig = stringVoice.Process(false);
                    break;

                case MACRO_B_PARTICLE:
                    particle.SetFreq(current.waveformFreq);
                    particle.SetResonance(0.15f + (timbre * 0.65f) + (harmonics * 0.25f));
                    particle.SetDensity(timbre);
                    particle.SetRandomFreq(0.35f + (timbre * 10.0f) + (morph * 10.0f));
                    particle.SetSpread(1.5f + (timbre * 5.0f) + (harmonics * 9.0f));
                    particle.SetGain(0.50f + (morph * 0.48f));
                    sig = particle.Process();
                    break;

                case MACRO_B_RING_MOD_NOISE:
                    grainlet.SetFreq(current.waveformFreq);
                    grainlet.SetFormantFreq(current.waveformFreq
                                             * (2.0f + (timbre * 4.0f) + (harmonics * 8.0f)));
                    grainlet.SetShape(0.15f + (timbre * 1.85f) + (morph * 0.55f));
                    grainlet.SetBleed(fclamp(timbre * (1.0f - morph * 0.35f) + harmonics * 0.25f,
                                             0.0f,
                                             1.0f));
                    sig = grainlet.Process();
                    break;

                case MACRO_B_OVERDRIVE:
                {
                    osc.SetWaveform(Oscillator::WAVE_SAW);
                    const float src    = osc.Process();
                    const float drive  = fclamp(timbre, 0.0f, 1.0f);
                    const float preAmp = 0.30f + harmonics * 0.95f;
                    overdrive.SetDrive(drive);
                    const float wet = overdrive.Process(src * preAmp);
                    // morph: lean dry -> full wet; keep a little wet at morph=0 so timbre stays audible
                    const float mix = fclamp(0.22f + morph * 0.78f, 0.0f, 1.0f);
                    sig = (src * (1.0f - mix)) + (wet * mix);
                    break;
                }

                case MACRO_B_BASS_DRUM_CLICK:
                {
                    drumPhase += current.waveformFreq / sampleRate;
                    bool trig = false;
                    if(drumPhase >= 1.0f)
                    {
                        drumPhase -= 1.0f;
                        trig = true;
                    }
                    synthBassDrum.SetFreq(fmaxf(20.0f, current.waveformFreq));
                    synthBassDrum.SetTone(timbre);
                    synthBassDrum.SetDecay(0.12f + (harmonics * 0.88f));
                    synthBassDrum.SetDirtiness(fclamp(timbre * 0.55f + morph * 0.45f, 0.0f, 1.0f));
                    synthBassDrum.SetFmEnvelopeAmount(fclamp(timbre * 0.35f + morph * 0.65f, 0.0f, 1.0f));
                    synthBassDrum.SetFmEnvelopeDecay(0.18f + (morph * 0.62f) + (timbre * 0.2f));
                    sig = synthBassDrum.Process(trig);
                    break;
                }

                default:
                    sig = 0.0f;
                    break;
            }
        }

        const float modelGain = CurrentModelOutputGain(current);
        const float voiced    = ApplyFinalLimiter(sig * modelGain);
        out[0][i]             = voiced;
        out[1][i]             = voiced;
    }
}

int main(void) {

    spark.Init();

    // Give I2C LED Driver and other components a chance to wake up.
    System::Delay(500);

    SparkSettings defaultSettings;
    defaultSettings.sparkMode    = MODE_WAVEFORMS;
    defaultSettings.waveform     = WAVE_SIN;
    defaultSettings.waveformFreq = 220.0f;
    defaultSettings.macroA = 0;
    defaultSettings.macroB = 0;

    storage.Init(defaultSettings);
    // Neutral startup voice in Wave bank.
    storage.GetSettings().sparkMode = MODE_WAVEFORMS;
    storage.GetSettings().macroA    = WrapIndex(storage.GetSettings().macroA, MACRO_A_COUNT);
    storage.GetSettings().macroB    = WrapIndex(storage.GetSettings().macroB, MACRO_B_COUNT);
    spark.SetAudioBlockSize(4);
    sampleRate = spark.AudioSampleRate();

    osc.Init(sampleRate);
    osc.SetAmp(0.70f);
    supersawLow.Init(sampleRate);
    supersawLow.SetAmp(0.70f);
    supersawLow.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    supersawHigh.Init(sampleRate);
    supersawHigh.SetAmp(0.70f);
    supersawHigh.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    variableSaw.Init(sampleRate);
    variableShape.Init(sampleRate);
    vosim.Init(sampleRate);
    formantOsc.Init(sampleRate);
    zOsc.Init(sampleRate);
    fm2.Init(sampleRate);
    harmonicOsc.Init(sampleRate);
    stringVoice.Init(sampleRate);
    particle.Init(sampleRate);
    synthBassDrum.Init(sampleRate);
    grainlet.Init(sampleRate);
    overdrive.Init();
    spark.SetLedCalibration(Spark::LedTarget::Onboard, kOnboardLedCalibration);
    spark.SetLedCalibration(Spark::LedTarget::Encoder, kEncoderI2cLedCalibration);
#if SPARK_ENCODER_I2C_ADDR7 != 0u
    spark.ConfigureEncoderI2cRgb(
        static_cast<uint8_t>(SPARK_ENCODER_I2C_ADDR7 & 0x7Fu),
        Spark::EncoderI2cRgbFormat::Reg8WriteDataAtAddressRgb8,
        static_cast<uint8_t>(SPARK_ENCODER_I2C_REG & 0xFFu));
#endif
    DebugInit();
    spark.StartAdc();
    startupWhiteStartMs = System::GetNow();
    SetStartupWhiteLeds();

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    shapeParam.Init(spark.knob2, 0.0f, 1.0f, Parameter::LINEAR);

    spark.StartAudio(AudioCallback);

    while(1)
    {
        runtime.ProcessControls();

        const uint32_t nowMs = System::GetNow();
        if(spark.button1.RisingEdge())
        {
            sw1PressStartMs    = nowMs;
            sw1ModifierActive  = false;
        }
        if(spark.button1.Pressed() && !sw1ModifierActive
           && ((nowMs - sw1PressStartMs) >= kSw1HoldMs))
        {
            sw1ModifierActive = true;
            LogModifierState("sw1 hold", "on");
            DebugStatusNow("sw1-mod-on");
        }

        if(spark.button2.RisingEdge())
        {
            sw2PressStartMs   = nowMs;
            sw2ModifierActive = false;
        }
        if(spark.button2.Pressed() && !sw2ModifierActive
           && ((nowMs - sw2PressStartMs) >= kSw2HoldMs))
        {
            sw2ModifierActive = true;
            LogModifierState("sw2 hold", "on");
            DebugStatusNow("sw2-mod-on");
        }

        if(spark.button1.FallingEdge())
        {
            if(sw1ModifierActive)
            {
                sw1ModifierActive = false;
                LogModifierState("sw1 hold", "off");
                DebugStatusNow("sw1-mod-off");
            }
        }
        if(spark.button2.FallingEdge())
        {
            if(sw2ModifierActive)
            {
                sw2ModifierActive = false;
                LogModifierState("sw2 hold", "off");
                DebugStatusNow("sw2-mod-off");
            }
        }

        ProcessEncoder();
        ProcessKnobs();
        if(startupWhiteActive)
        {
            const uint32_t nowMs = System::GetNow();
            if((nowMs - startupWhiteStartMs) >= kStartupWhiteHoldMs)
            {
                // Exit startup white only after hold time and at least one control/voltage cycle.
                startupWhiteActive = false;
            }
        }
        UpdateLeds();
        DebugMaybePrintI2cScan();
        DebugMaybeStatus();

        if(runtime.MaybeSave(storage, SAVE_DELAY_MS))
        {
            DebugLog(DBG_INFO, DBG_CAT_STORAGE, "settings saved");
        }
    }
}