#include "daisysp.h"
#include "daisy_spark.h"
using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

static Spark            spark;
static SparkDiagnostics diagnostics(spark);
static SparkRuntime     runtime(spark, diagnostics);

static Oscillator osc;
static Parameter  freqParam;
static Parameter  cutoffParam;

static const uint32_t SAVE_DELAY_MS = 3000U;
static float          sampleRate    = 48000.0f;
static float          lpState       = 0.0f;
static float          hpState       = 0.0f;
static float          lastIn        = 0.0f;

enum FilterModel
{
    FILTER_LP = 0,
    FILTER_HP,
    FILTER_BP,
    FILTER_COUNT
};

struct FilterSettings
{
    int   model;
    float oscFreq;
    float cutoff;

    bool operator!=(const FilterSettings& a) const
    {
        return a.model != model || a.oscFreq != oscFreq || a.cutoff != cutoff;
    }
};

PersistentStorage<FilterSettings> storage(spark.seed.qspi);

#ifndef SPARK_DEBUG_DEFAULT_LEVEL
#define SPARK_DEBUG_DEFAULT_LEVEL 2
#endif

#ifndef SPARK_DEBUG_DEFAULT_MASK
#define SPARK_DEBUG_DEFAULT_MASK 0x0F
#endif

#ifndef SPARK_DEBUG_STATUS_INTERVAL_MS
#define SPARK_DEBUG_STATUS_INTERVAL_MS 500U
#endif

static void DebugInit()
{
    diagnostics.Init(SPARK_DEBUG_DEFAULT_LEVEL, SPARK_DEBUG_DEFAULT_MASK, "filters");
    diagnostics.Log(DBG_INFO, DBG_CAT_STATE, "filters scaffold ready");
}

static void DebugMaybeStatus()
{
    if(!diagnostics.StatusDue(SPARK_DEBUG_STATUS_INTERVAL_MS))
    {
        return;
    }
    FilterSettings& current = storage.GetSettings();
    diagnostics.RefreshStatusLine("filters",
                                  current.model,
                                  0,
                                  0,
                                  current.oscFreq,
                                  spark.knob1.Value(),
                                  spark.knob2.Value(),
                                  spark.encoder.Pressed(),
                                  spark.button1.Pressed(),
                                  spark.button2.Pressed());
}

static void ProcessControls()
{
    FilterSettings& current = storage.GetSettings();
    const int32_t   inc     = spark.encoder.Increment();
    if(inc != 0)
    {
        current.model = WrapIndex(current.model + static_cast<int>(inc), FILTER_COUNT);
        runtime.MarkInteraction();
        diagnostics.Log(DBG_INFO, DBG_CAT_CTRL, "filter model -> %d", current.model);
    }

    const float newFreq = freqParam.Process();
    if(fabsf(newFreq - current.oscFreq) > 0.2f)
    {
        current.oscFreq = newFreq;
        runtime.MarkInteraction();
    }

    const float newCutoff = cutoffParam.Process();
    if(fabsf(newCutoff - current.cutoff) > 0.005f)
    {
        current.cutoff = newCutoff;
        runtime.MarkInteraction();
    }
}

static float ApplyOnePole(FilterSettings& current, float in)
{
    const float cutoffHz = fmaxf(20.0f, current.cutoff * 8000.0f);
    const float alpha    = expf(-2.0f * PI_F * cutoffHz / sampleRate);
    lpState              = (alpha * lpState) + ((1.0f - alpha) * in);
    const float hp       = in - lpState;
    hpState              = hp;
    const float bp       = hpState * 0.5f + (lastIn - in) * 0.5f;
    lastIn               = in;

    switch(current.model)
    {
        case FILTER_HP: return hp;
        case FILTER_BP: return bp;
        case FILTER_LP:
        default: return lpState;
    }
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    FilterSettings& current = storage.GetSettings();
    osc.SetFreq(current.oscFreq);
    osc.SetWaveform(Oscillator::WAVE_SAW);

    for(size_t i = 0; i < size; i++)
    {
        const float src = osc.Process();
        const float sig = ApplyOnePole(current, src);
        out[0][i]       = sig * 0.4f;
        out[1][i]       = sig * 0.4f;
    }
}

int main(void)
{
    spark.Init();
    System::Delay(250);

    FilterSettings defaults;
    defaults.model   = FILTER_LP;
    defaults.oscFreq = 220.0f;
    defaults.cutoff  = 0.5f;
    storage.Init(defaults);

    spark.SetAudioBlockSize(4);
    sampleRate = spark.AudioSampleRate();
    osc.Init(sampleRate);
    osc.SetAmp(1.0f);

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    cutoffParam.Init(spark.knob2, 0.01f, 1.0f, Parameter::LINEAR);

    DebugInit();
    spark.StartAudio(AudioCallback);

    while(1)
    {
        runtime.ProcessControls();
        ProcessControls();
        DebugMaybeStatus();

        if(runtime.MaybeSave(storage, SAVE_DELAY_MS))
        {
            diagnostics.Log(DBG_INFO, DBG_CAT_STORAGE, "filters settings saved");
        }
    }
}
