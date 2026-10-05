// asswrite.hpp —— 规范输出（架构 §5.3；S6：Aegisub 无错打开）
// 输出规范：UTF-8 BOM + CRLF；Dialogue 行按标准 V4+ 列序重排（时间本就要改写，
// 顺带统一格式）；Text 字段字节级原样；Style 行取 rawLine 仅替换名字段（D3）。
#pragma once

#include <QByteArray>

#include "assdoc.hpp"

namespace suture {

QByteArray serializeAss(const AssDocument& doc);

}  // namespace suture
