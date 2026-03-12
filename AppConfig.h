#pragma once

inline constexpr bool kFreeVersion    = true;
inline constexpr int  kMaxChannels    = kFreeVersion ? 2 : 6;
inline constexpr int  kMaxMuteGroups  = kFreeVersion ? 1 : 4;
