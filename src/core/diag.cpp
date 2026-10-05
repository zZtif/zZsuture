#include "diag.hpp"

namespace suture {

const char* diagCodeId(DiagCode code)
{
    switch (code) {
    case DiagCode::EncodingUncertain:
    case DiagCode::EncodingHeuristic:    return "P1";
    case DiagCode::EpisodeUnrecognized:  return "P6";
    case DiagCode::ParseQuirk:           return "P2";
    case DiagCode::MalformedLine:        return "—";
    case DiagCode::ZeroDurationIgnored:  return "P3";
    case DiagCode::CommentIgnoredStat:   return "P4";
    case DiagCode::GarbageDropped:       return "P5";
    case DiagCode::EmbeddedDropped:      return "P10";
    case DiagCode::UnsupportedSsaV4:     return "P11";
    case DiagCode::StyleNameDuplicate:   return "S1";
    case DiagCode::TimerNot100:          return "T5";
    case DiagCode::UndefinedStyleRef:    return "S3";
    case DiagCode::ScriptInfoConflict:   return "S4";
    case DiagCode::RTagRewritten:        return "S7";
    case DiagCode::StyleIsolated:        return "S2";
    case DiagCode::DurationMismatch:     return "C1";
    case DiagCode::BoundaryOverlap:
    case DiagCode::BoundaryGap:          return "C2";
    case DiagCode::PlayResMismatch:      return "C3";
    case DiagCode::MplsUnsupported:      return "T1";
    default:                             return "—";
    }
}

}  // namespace suture
