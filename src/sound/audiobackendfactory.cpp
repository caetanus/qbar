#include "audiobackendfactory.h"

#include "soundmodel.h"

#ifdef QBAR_HAVE_WIREPLUMBER
#include "wireplumberbackend.h"
#endif

AudioBackend *createAudioBackend(QObject *parent)
{
#ifdef QBAR_HAVE_WIREPLUMBER
    return new WirePlumberBackend(parent);
#else
    return new SoundModel(parent);
#endif
}
