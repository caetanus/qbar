#pragma once

class QObject;
class AudioBackend;

// Returns the audio backend selected at build time: WirePlumber when compiled
// with -DQBAR_HAVE_WIREPLUMBER (wireplumber-0.5 present), otherwise the libpulse
// SoundModel. Sound.qml consumes whichever via the `soundModel` context property.
AudioBackend *createAudioBackend(QObject *parent = nullptr);
