#include "engine/Engine.h"

#include "engine/io/AudioDevice.h"

namespace ses::engine {

const char* engineModuleName()
{
    return "ses_engine (miniaudio device + RT mixer)";
}

// ---- DeviceEngine (ROADMAP P1 task 3) -------------------------------------------
// Off-RT only: everything here runs on the GUI / test thread. Transport
// commands cross the SpscRing and are applied by the audio callback at block
// boundaries (DESIGN §4.4); state() reads the RT atomics without blocking.

DeviceEngine::DeviceEngine() = default;
DeviceEngine::~DeviceEngine() = default;

bool DeviceEngine::open(int sampleRate, unsigned periodFrames)
{
    auto dev = std::make_unique<AudioDevice>();
    if (!dev->open(sampleRate, periodFrames))
        return false;
    device_ = std::move(dev);
    return true;
}

bool DeviceEngine::openOnNullBackend(int sampleRate, unsigned periodFrames)
{
    auto dev = std::make_unique<AudioDevice>();
    if (!dev->openOnBackend(ma_backend_null, sampleRate, periodFrames))
        return false;
    device_ = std::move(dev);
    return true;
}

bool DeviceEngine::start()
{
    return device_ && device_->start();
}

void DeviceEngine::close()
{
    device_.reset();
}

bool DeviceEngine::available() const
{
    return device_ && device_->isOpen();
}

void DeviceEngine::play()
{
    if (!available())
        return;
    if (!device_->started())
        device_->start(); // lazy pump start; see header
    device_->sendCommand({ RtCommand::Play });
}

void DeviceEngine::stop()
{
    if (!available())
        return;
    device_->sendCommand({ RtCommand::Stop });
}

void DeviceEngine::seek(int64_t sample)
{
    if (!available())
        return;
    device_->sendCommand({ RtCommand::Seek, sample, 0, 0.0f });
}

void DeviceEngine::setLoop(bool on, int64_t start, int64_t end)
{
    loop_ = on;
    loopStart_ = start;
    loopEnd_ = end;
    if (!available())
        return;
    device_->sendCommand({ RtCommand::SetLoop, start, end, on ? 1.0f : 0.0f });
}

void DeviceEngine::setAbFader(double x)
{
    if (!available())
        return;
    device_->sendCommand({ RtCommand::SetAbFader, 0, 0, static_cast<float>(x) });
}

TransportState DeviceEngine::state() const
{
    if (!available())
        return {};
    TransportState st;
    st.playing = device_->playing();
    st.loop = loop_;
    st.playhead = device_->playhead();
    st.loopStart = loopStart_;
    st.loopEnd = loopEnd_;
    return st;
}

bool DeviceEngine::nextTelemetry(Telemetry& out)
{
    return device_ && device_->nextTelemetry(out);
}

bool DeviceEngine::nextDeviceEvent(DeviceEvent& out)
{
    return device_ && device_->nextDeviceEvent(out);
}

int DeviceEngine::actualSampleRate() const
{
    return device_ ? device_->actualSampleRate() : 0;
}

} // namespace ses::engine