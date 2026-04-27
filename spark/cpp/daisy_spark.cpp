#include "daisy_spark.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>

#ifndef SPARK_DIAG_BANNER
#define SPARK_DIAG_BANNER 1
#endif

#ifndef SAMPLE_RATE
//#define SAMPLE_RATE DSY_AUDIO_SAMPLE_RATE
#define SAMPLE_RATE 48014.f
#endif

using namespace daisy_spark;

// Compatible with Seed Rev3 and Rev4
constexpr Pin SW_1_PIN       = seed::D27;
constexpr Pin SW_2_PIN       = seed::D28;

constexpr Pin KNOB_1_PIN     = seed::D21;
constexpr Pin KNOB_2_PIN     = seed::D15;

constexpr Pin ENC_A_PIN      = seed::D26;
constexpr Pin ENC_B_PIN      = seed::D25;
constexpr Pin ENC_CLICK_PIN  = seed::D13;

constexpr Pin LED_1_R_PIN    = seed::D20;
constexpr Pin LED_1_G_PIN    = seed::D19;
constexpr Pin LED_1_B_PIN    = seed::D18;

constexpr Pin LED_2_R_PIN    = seed::D17;
constexpr Pin LED_2_G_PIN    = seed::D24;
constexpr Pin LED_2_B_PIN    = seed::D23;

// Spark peripheral I2C bus (matches Daisy Pod-style expansion bus pins).
constexpr Pin PERIPH_I2C_SCL_PIN = seed::D12;
constexpr Pin PERIPH_I2C_SDA_PIN = seed::D11;

void Spark::Init(bool boost)
{
    // Set Some numbers up for accessors.
    // Initialize the hardware.
    seed.Configure();
    seed.Init(boost);
    InitButtons();
    InitEncoder();
    InitLeds();
    InitKnobs();
    InitMidi();
    SetAudioBlockSize(48);
}

void Spark::DelayMs(size_t del)
{
    seed.DelayMs(del);
}


void Spark::SetHidUpdateRates()
{
    for(int i = 0; i < KNOB_LAST; i++)
    {
        knobs[i]->SetSampleRate(AudioCallbackRate());
    }
}

void Spark::StartAudio(AudioHandle::InterleavingAudioCallback cb)
{
    seed.StartAudio(cb);
}

void Spark::StartAudio(AudioHandle::AudioCallback cb)
{
    seed.StartAudio(cb);
}

void Spark::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void Spark::ChangeAudioCallback(AudioHandle::AudioCallback cb)
{
    seed.ChangeAudioCallback(cb);
}

void Spark::StopAudio()
{
    seed.StopAudio();
}

void Spark::SetAudioBlockSize(size_t size)
{
    seed.SetAudioBlockSize(size);
    SetHidUpdateRates();
}

size_t Spark::AudioBlockSize()
{
    return seed.AudioBlockSize();
}

void Spark::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    seed.SetAudioSampleRate(samplerate);
    SetHidUpdateRates();
}

float Spark::AudioSampleRate()
{
    return seed.AudioSampleRate();
}

float Spark::AudioCallbackRate()
{
    return seed.AudioCallbackRate();
}

void Spark::StartAdc()
{
    seed.adc.Start();
}

void Spark::StopAdc()
{
    seed.adc.Stop();
}


void Spark::ProcessAnalogControls()
{
    knob1.Process();
    knob2.Process();
}

float Spark::GetKnobValue(Knob k)
{
    size_t idx;
    idx = k < KNOB_LAST ? k : KNOB_1;
    return knobs[idx]->Value();
}

void Spark::ProcessDigitalControls()
{
    encoder.Debounce();
    button1.Debounce();
    button2.Debounce();
}

bool Spark::InitPeripheralI2c()
{
    if(peripheral_i2c_ready_)
    {
        return true;
    }

    I2CHandle::Config i2c_cfg;
    i2c_cfg.periph          = I2CHandle::Config::Peripheral::I2C_1;
    i2c_cfg.mode            = I2CHandle::Config::Mode::I2C_MASTER;
    i2c_cfg.speed           = I2CHandle::Config::Speed::I2C_100KHZ;
    i2c_cfg.pin_config.scl  = PERIPH_I2C_SCL_PIN;
    i2c_cfg.pin_config.sda  = PERIPH_I2C_SDA_PIN;

    peripheral_i2c_ready_ = (peripheral_i2c.Init(i2c_cfg) == I2CHandle::Result::OK);
    return peripheral_i2c_ready_;
}

uint8_t Spark::ScanI2cDevices(uint8_t* out_addresses,
                              uint8_t  max_addresses,
                              uint8_t  start_addr,
                              uint8_t  end_addr,
                              uint32_t timeout_ms)
{
    if(!peripheral_i2c_ready_ || out_addresses == nullptr || max_addresses == 0
       || start_addr > end_addr)
    {
        return 0;
    }

    uint8_t found  = 0;
    uint8_t rxByte = 0;
    for(uint8_t addr = start_addr; addr <= end_addr && found < max_addresses; addr++)
    {
        if(peripheral_i2c.ReceiveBlocking(addr, &rxByte, 1, timeout_ms)
           == I2CHandle::Result::OK)
        {
            out_addresses[found++] = addr;
        }
    }
    return found;
}

void Spark::ClearLeds()
{
    // Using Color
    Color c;
    c.Init(Color::PresetColor::OFF);
    led1.SetColor(c);
    led2.SetColor(c);
    // Without
    // led1.Set(0.0f, 0.0f, 0.0f);
    // led2.Set(0.0f, 0.0f, 0.0f);
}

void Spark::UpdateLeds()
{
    led1.Update();
    led2.Update();
}

void Spark::ConfigureEncoderI2cRgb(uint8_t                 device_addr7,
                                   Spark::EncoderI2cRgbFormat format,
                                   uint8_t                 reg_or_cmd)
{
    encoder_i2c_addr7_  = (format == Spark::EncoderI2cRgbFormat::Disabled) ? 0U : device_addr7;
    encoder_i2c_format_   = format;
    encoder_i2c_reg_     = reg_or_cmd;
    encoder_i2c_last_r8_ = 0xFF;
    encoder_i2c_last_g8_ = 0xFF;
    encoder_i2c_last_b8_ = 0xFF;
}

void Spark::SetEncoderI2cRgb(float r, float g, float b)
{
    if(encoder_i2c_format_ == Spark::EncoderI2cRgbFormat::Disabled || encoder_i2c_addr7_ == 0U)
    {
        return;
    }
    if(!peripheral_i2c_ready_ && !InitPeripheralI2c())
    {
        return;
    }
    float cr, cg, cb;
    ApplyLedCalibration(LedTarget::Encoder, r, g, b, cr, cg, cb);
    const auto toByte = [](float u) {
        return static_cast<uint8_t>(fminf(255.0f, fmaxf(0.0f, u * 255.0f + 0.5f)));
    };
    const uint8_t r8 = toByte(cr);
    const uint8_t g8 = toByte(cg);
    const uint8_t b8 = toByte(cb);
    if(r8 == encoder_i2c_last_r8_ && g8 == encoder_i2c_last_g8_ && b8 == encoder_i2c_last_b8_)
    {
        return;
    }
    encoder_i2c_last_r8_ = r8;
    encoder_i2c_last_g8_ = g8;
    encoder_i2c_last_b8_ = b8;

    I2CHandle::Result st = I2CHandle::Result::ERR;
    const uint8_t     addr7 = encoder_i2c_addr7_ & 0x7F;

    switch(encoder_i2c_format_)
    {
        case Spark::EncoderI2cRgbFormat::Rgb8Only:
        {
            uint8_t buf[3] = {r8, g8, b8};
            st             = peripheral_i2c.TransmitBlocking(addr7, buf, 3, 20);
        }
        break;
        case Spark::EncoderI2cRgbFormat::Reg8ThenRgb8:
        {
            uint8_t buf[4] = {encoder_i2c_reg_, r8, g8, b8};
            st             = peripheral_i2c.TransmitBlocking(addr7, buf, 4, 20);
        }
        break;
        case Spark::EncoderI2cRgbFormat::Reg8WriteDataAtAddressRgb8:
        {
            uint8_t         rgb[3] = {r8, g8, b8};
            const uint16_t regw = static_cast<uint16_t>(encoder_i2c_reg_);
            st
                = peripheral_i2c.WriteDataAtAddress(addr7, regw, 1, rgb, 3, 20);
        }
        break;
        case Spark::EncoderI2cRgbFormat::Disabled:
        default: break;
    }
    (void)st;
}

void Spark::SetLedCalibration(LedTarget target, const LedCalibration& calibration)
{
    if(target == LedTarget::Encoder)
    {
        encoder_led_calibration_ = calibration;
        return;
    }
    onboard_led_calibration_ = calibration;
}

void Spark::ApplyLedCalibration(LedTarget target,
                                float     in_r,
                                float     in_g,
                                float     in_b,
                                float&    out_r,
                                float&    out_g,
                                float&    out_b) const
{
    const LedCalibration& cal = (target == LedTarget::Encoder) ? encoder_led_calibration_
                                                                : onboard_led_calibration_;
    const float g             = (cal.gamma <= 0.0f) ? 1.0f : cal.gamma;

    out_r = ApplyGamma(Clamp01(in_r * cal.red_gain * cal.global_gain), g);
    out_g = ApplyGamma(Clamp01(in_g * cal.green_gain * cal.global_gain), g);
    out_b = ApplyGamma(Clamp01(in_b * cal.blue_gain * cal.global_gain), g);
}

float Spark::Clamp01(float v)
{
    if(v < 0.0f)
    {
        return 0.0f;
    }
    if(v > 1.0f)
    {
        return 1.0f;
    }
    return v;
}

float Spark::ApplyGamma(float v, float gamma)
{
    return powf(Clamp01(v), gamma);
}

void Spark::InitButtons()
{
    // button1
    button1.Init(SW_1_PIN);
    // button2
    button2.Init(SW_2_PIN);

    buttons[BUTTON_1] = &button1;
    buttons[BUTTON_2] = &button2;
}

void Spark::InitEncoder()
{
    encoder.Init(ENC_A_PIN, ENC_B_PIN, ENC_CLICK_PIN);
}

void Spark::InitLeds()
{
    // LEDs are just going to be on/off for now.
    // TODO: Add PWM support
    led1.Init(LED_1_R_PIN, LED_1_G_PIN, LED_1_B_PIN, true);

    led2.Init(LED_2_R_PIN, LED_2_G_PIN, LED_2_B_PIN, true);

    ClearLeds();
    UpdateLeds();
}
void Spark::InitKnobs()
{
    // Configure the ADC channels using the desired pin
    AdcChannelConfig knob_init[KNOB_LAST];
    knob_init[KNOB_1].InitSingle(KNOB_1_PIN);
    knob_init[KNOB_2].InitSingle(KNOB_2_PIN);
    // Initialize with the knob init struct w/ 2 members
    // Set Oversampling to 32x
    seed.adc.Init(knob_init, KNOB_LAST);
    // Make an array of pointers to the knobs.
    knobs[KNOB_1] = &knob1;
    knobs[KNOB_2] = &knob2;
    for(int i = 0; i < KNOB_LAST; i++)
    {
        knobs[i]->Init(seed.adc.GetPtr(i), seed.AudioCallbackRate());
    }
}
void Spark::InitMidi()
{
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);
}

void SparkDiagnostics::Init(uint8_t level, uint8_t mask, const char* firmware_name)
{
    level_ = level;
    mask_  = mask;
    spark_.seed.StartLog();
#if SPARK_DIAG_BANNER
    PrintBanner(firmware_name);
#else
    (void)firmware_name;
#endif
}

void SparkDiagnostics::PrintBanner(const char* firmware_name)
{
    spark_.seed.PrintLine("========================================");
    spark_.seed.PrintLine("Spark Diagnostics Firmware: %s", firmware_name);
    spark_.seed.PrintLine("Controls:");
    spark_.seed.PrintLine("- Turn encoder: select waveform/model");
    spark_.seed.PrintLine("- Press+turn encoder: change bank");
    spark_.seed.PrintLine("- Knob1: primary, Knob2: secondary");
    spark_.seed.PrintLine("Current debug level=%d mask=0x%02x", level_, mask_);
    spark_.seed.PrintLine("========================================");
}

void SparkDiagnostics::Log(uint8_t level, uint8_t category, const char* format, ...)
{
    if(level > level_ || (mask_ & category) == 0)
    {
        return;
    }

    char    line[192];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    spark_.seed.PrintLine("%s", line);
}

void SparkDiagnostics::LogModelChange(const char* domain, int model_index, const char* model_name)
{
    Log(DBG_INFO,
        DBG_CAT_CTRL,
        "%s -> %d (%s)",
        domain,
        model_index,
        model_name ? model_name : "unknown");
}

void SparkDiagnostics::LogStatusLine(const char* mode_name,
                                     int         primary_index,
                                     int         secondary_a,
                                     int         secondary_b,
                                     float       frequency_hz,
                                     float       knob1_value,
                                     float       knob2_value,
                                     bool        encoder_pressed,
                                     bool        button1_pressed,
                                     bool        button2_pressed)
{
    Log(DBG_INFO,
        DBG_CAT_STATE,
        "status mode=%s wf=%d a=%d b=%d freq=%.2f k1=%.3f k2=%.3f enc=%d b1=%d b2=%d",
        mode_name,
        primary_index,
        secondary_a,
        secondary_b,
        frequency_hz,
        knob1_value,
        knob2_value,
        encoder_pressed ? 1 : 0,
        button1_pressed ? 1 : 0,
        button2_pressed ? 1 : 0);
}

void SparkDiagnostics::RefreshStatusLine(const char* mode_name,
                                         int         primary_index,
                                         int         secondary_a,
                                         int         secondary_b,
                                         float       frequency_hz,
                                         float       knob1_value,
                                         float       knob2_value,
                                         bool        encoder_pressed,
                                         bool        button1_pressed,
                                         bool        button2_pressed)
{
    // Keep this comfortably below LOGGER_BUFFER (128) to avoid "$$" overflow markers.
    // Also keep redraw width narrow enough to avoid terminal wrap.
    char line[96];
    const int freq_i = static_cast<int>(frequency_hz);
    const int k1_i   = static_cast<int>(knob1_value * 100.0f);
    const int k2_i   = static_cast<int>(knob2_value * 100.0f);
    snprintf(line,
             sizeof(line),
             "mode=%s wf=%d a=%d b=%d f=%d k1=%d%% k2=%d%% e%d b%d%d",
             mode_name,
             primary_index,
             secondary_a,
             secondary_b,
             freq_i,
             k1_i,
             k2_i,
             encoder_pressed ? 1 : 0,
             button1_pressed ? 1 : 0,
             button2_pressed ? 1 : 0);

    if(DBG_INFO > level_ || (mask_ & DBG_CAT_STATE) == 0)
    {
        return;
    }
    // Redraw the same console line without adding scrollback.
    spark_.seed.Print("\r%-72s", line);
}

void SparkDiagnostics::RefreshCustomStatusLine(const char* line, uint8_t pad_width)
{
    if(DBG_INFO > level_ || (mask_ & DBG_CAT_STATE) == 0)
    {
        return;
    }
    spark_.seed.Print("\r%-*s", static_cast<int>(pad_width), line ? line : "");
}

void SparkDiagnostics::LogHeartbeat(const char* firmware_name, uint32_t interval_ms)
{
    const uint32_t now = System::GetNow();
    if(now - last_heartbeat_ms_ < interval_ms)
    {
        return;
    }
    last_heartbeat_ms_ = now;
    Log(DBG_INFO,
        DBG_CAT_STATE,
        "heartbeat fw=%s t_ms=%lu level=%u mask=0x%02x",
        firmware_name,
        static_cast<unsigned long>(now),
        static_cast<unsigned>(Level()),
        static_cast<unsigned>(Mask()));
}

bool SparkDiagnostics::StatusDue(uint32_t interval_ms)
{
    const uint32_t now = System::GetNow();
    if(now - last_status_ms_ < interval_ms)
    {
        return false;
    }
    last_status_ms_ = now;
    return true;
}

void SparkRuntime::MarkInteraction()
{
    dirty_               = true;
    last_interaction_ms_ = System::GetNow();
}
