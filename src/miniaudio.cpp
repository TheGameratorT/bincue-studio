// The single translation unit that compiles miniaudio's implementation. Every
// other file includes <miniaudio.h> for the declarations only, so this heavy
// blob is built exactly once and never re-touched by incremental builds.
//
// We drive a raw PCM playback device and feed it ourselves (see
// playbackengine.cpp), so the file decoders, encoders, tone generators and the
// high-level engine/node-graph/resource-manager layers are all switched off to
// keep this object small. The backends are dlopen'd at runtime, so there are no
// new link-time libraries beyond pthread/m/dl.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
