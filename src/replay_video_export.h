#pragma once
#include "jp_combat.h"
#include "match_replay.h"
namespace gbarecomp {
struct NativeFrameControl;
}
namespace seed {
ReplayPlaybackResult export_replay_video(gbarecomp::NativeFrameControl&, const MatchReplay&,
                                         const ReplayVideoExportRequest&);
}
