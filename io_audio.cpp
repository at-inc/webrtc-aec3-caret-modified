#include "io_audio.h"

#include "api/echo_canceller3_factory.h"
#include "api/echo_canceller3_config.h"
#include "audio_processing/include/audio_processing.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/high_pass_filter.h"

#include <QMediaDevices>
#include "QDebug"

IoAudio::IoAudio() {
    m_status = StatusType::Stop;

    m_app = NULL;
    m_audioSource = NULL;
    m_audioSink = NULL;
    m_audioInDevice = NULL;
    m_audioOutDevice = NULL;
    m_qobject_thread = NULL;
    m_api_thread = NULL;

    m_api_thread = QThread::create([&] {
        int sampleSize = MIN_SAMPLE_SIZE;
        while(m_status != StatusType::Destroy) {
            //
            // write to speakers
            if(!_audioOutBuf.isEmpty()) {
                if(m_audioSink != NULL && m_audioSink->bytesFree() >= sampleSize) {
                    auto item = _audioOutBuf.pop(sampleSize);
                    // write to out
                    m_audioOutDevice->write((const char*)item.data(), item.size());
                    // put to echo
                    _echoBuff.putData(item);
                }
            }
            //
            // read mic
            auto micData = m_audioInDevice != NULL ? m_audioInDevice->readAll() : QByteArray();
            if(!micData.isEmpty())  {
                auto data = std::vector<uint8_t>( micData.size());
                memcpy(data.data(), micData.data(), micData.size());
                _micBuf.putData(data);
            }
            //
            // process data before sending
            while(_micBuf.size() >= MIN_SAMPLE_SIZE) {
                auto raw = _micBuf.pop(MIN_SAMPLE_SIZE);
                auto out = std::vector<uint8_t>(MIN_SAMPLE_SIZE);

                while(_echoBuff.size() >= MIN_SAMPLE_SIZE) {
                    auto echoFrame = _echoBuff.pop(MIN_SAMPLE_SIZE);
                    aecPutFarEndFrame(echoFrame, MIN_SAMPLE_SIZE / 2);
                }
                aecProcess(raw, out, MIN_SAMPLE_SIZE / 2);
                _sendToNetBuf.putData(out);
            }
            //
            // data ready to sent
            while(_sendToNetBuf.size() >= sampleSize) {
                auto out = _sendToNetBuf.pop(sampleSize);
                // because of loopback - put data back to speakers
                _audioOutBuf.putData(out);
            }

            QThread::msleep(5);
        }
    });
    m_api_thread->start();
}

IoAudio::~IoAudio() {
    qDebug("%s: destroyed", TAG);
    m_status = StatusType::Destroy;

    _audioOutBuf.clear();
    _micBuf.clear();
    _echoBuff.clear();

    if(m_app != NULL) {
        m_app->exit(0);
        m_app = NULL;
    }
    while(m_api_thread->isRunning() || m_qobject_thread->isRunning()) {
        QThread::msleep(100);
    }
    m_api_thread->deleteLater();
    m_qobject_thread->deleteLater();

    if(m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
    }
    if(m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
    }
    destroyAec();
    m_audioSource = NULL;
    m_audioSink = NULL;
    m_audioInDevice = NULL;
    m_audioOutDevice = NULL;
}

void IoAudio::startAudio() {
    qDebug("%s: start", TAG);
    if(m_status == StatusType::Run) {
        qDebug("%s: startAudio, already started, return", TAG);
    }
    m_qobject_thread = QThread::create([this]{
        int arg = 0;
        QCoreApplication app(arg, NULL);
        auto initRes = initAudio(AudioMode::Audio16Khz);
        if(!initRes) {
            qDebug("%s: startAudio, init with error, return", TAG);
            return 0;
        }
        initAec(m_mode == AudioMode::Audio16Khz ? 16000 : 8000);
        if(m_status != StatusType::Destroy) {
            m_status = StatusType::Run;
        }
        m_app = &app;
        auto res = app.exec();
        return res;
    });
    m_qobject_thread->start();
    // wait for launching qt thread
    while(m_app == NULL || m_status == StatusType::Destroy) {
        QThread::msleep(100);
    }
}

bool IoAudio::initAudio(AudioMode mode) {
    QAudioFormat audio_format;
    m_mode = mode;
    audio_info_in = QMediaDevices::defaultAudioInput();
    audio_info_out = QMediaDevices::defaultAudioOutput();

    if(mode == AudioMode::Audio16Khz) {
        audio_format.setSampleRate(16000);
        audio_format.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    } else {
        audio_format.setSampleRate(8000);
        audio_format.setSampleFormat(QAudioFormat::SampleFormat::UInt8);
    }
    audio_format.setChannelCount(1);

    if (!audio_info_in.isFormatSupported(audio_format)) {
        qDebug("%s: input device does not support format", TAG);
        return false;
    }
    if (!audio_info_out.isFormatSupported(audio_format)) {
        qDebug("%s: output device does not support default format", TAG);
        return false;
    }
    if(m_audioSource != NULL) {
        m_audioSource->stop();
        m_audioInDevice = NULL;
        m_audioSource->deleteLater();
        m_audioSource = NULL;
    }
    if(m_audioSink != NULL) {
        m_audioSink->stop();
        m_audioOutDevice = NULL;
        m_audioSink->deleteLater();
        m_audioSink = NULL;
    }
    auto audio_in = std::shared_ptr<QAudioInput>(new QAudioInput());
    audio_in->setDevice(audio_info_in);
    auto audio_out = std::shared_ptr<QAudioOutput>(new QAudioOutput());
    audio_out->setDevice(audio_info_out);
    audio_in->setVolume(1);
    audio_out->setVolume(1);

    auto desc = audio_info_in.description();
    qDebug("%s: input device: %s", TAG, desc.toStdString().c_str());
    desc = audio_info_out.description();
    qDebug("%s: out device: %s", TAG, desc.toStdString().c_str());

    m_audioSink = new QAudioSink(audio_format);
    m_audioOutDevice = m_audioSink->start();

    m_audioSource = new QAudioSource(audio_format);
    m_audioInDevice = m_audioSource->start();

    qDebug("%s: sink size: %d", TAG, (int)m_audioSink->bufferSize());
    qDebug("%s: source size: %d", TAG, (int)m_audioSource->bufferSize());
    return true;
}

bool IoAudio::initAec(int sampleRate) {
    std::lock_guard echo_mux(_aecLock);
    int channels = 1;

    webrtc::EchoCanceller3Config aec_config;
    aec_config.filter.export_linear_aec_output = false;

    webrtc::EchoCanceller3Factory aec_factory = webrtc::EchoCanceller3Factory(aec_config);
    echo_controler = aec_factory.Create(sampleRate, channels, channels);
    hp_filter = std::make_shared<webrtc::HighPassFilter>(sampleRate, channels);

    webrtc::StreamConfig config = webrtc::StreamConfig(sampleRate, channels, true);

    ref_audio = std::make_shared<webrtc::AudioBuffer>(
        config.sample_rate_hz(), config.num_channels(),
        config.sample_rate_hz(), config.num_channels(),
        config.sample_rate_hz(), config.num_channels());
    aec_audio = std::make_shared<webrtc::AudioBuffer>(
        config.sample_rate_hz(), config.num_channels(),
        config.sample_rate_hz(), config.num_channels(),
        config.sample_rate_hz(), config.num_channels());

    return true;
}

void IoAudio::aecPutFarEndFrame(std::vector<uint8_t> &in, int samplesCount) {
    std::lock_guard echo_mux(_aecLock);
    int sampleRate = m_mode == AudioMode::Audio16Khz ? 16000 : 8000;
    size_t samples = std::min(samplesCount, sampleRate / 100);
    if (samples == 0) return;
    size_t nCount = (samplesCount / samples);
    int channels = 1;

    for (size_t i = 0; i < nCount; i++) {
        ref_frame.UpdateFrame(0, (int16_t*) in.data(), samples, sampleRate,
                              webrtc::AudioFrame::kNormalSpeech,
                              webrtc::AudioFrame::kVadActive, channels);
        ref_audio->CopyFrom(&ref_frame);
        hp_filter->Process(ref_audio.get(), false);
        echo_controler->AnalyzeRender(ref_audio.get());
    }
}

void IoAudio::aecProcess(std::vector<uint8_t> &in,
                         std::vector<uint8_t> &out,
                         int samplesCount) {
    std::lock_guard echo_mux(_aecLock);
    int sampleRate = m_mode == AudioMode::Audio16Khz ? 16000 : 8000;
    size_t samples = std::min(samplesCount, sampleRate / 100);
    if (samples == 0) return;
    size_t nCount = (samplesCount / samples);
    int channels = 1;

    for (size_t i = 0; i < nCount; i++) {
        aec_frame.UpdateFrame(0, (int16_t*) in.data(), samples, sampleRate,
                              webrtc::AudioFrame::kNormalSpeech,
                              webrtc::AudioFrame::kVadActive, channels);
        aec_audio->CopyFrom(&aec_frame);
        hp_filter->Process(aec_audio.get(), false);
        echo_controler->AnalyzeCapture(aec_audio.get());
        echo_controler->ProcessCapture(aec_audio.get(), true);
        aec_audio->CopyTo(&aec_frame);
        memcpy(out.data(), aec_frame.data(), samples * 2);
    }
}

void IoAudio::destroyAec() {
    std::lock_guard echo_mux(_aecLock);
    // all on make_shared - so no actions
}
