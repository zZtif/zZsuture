// textdec.hpp —— 编码探测与解码（P1；架构 §5.1）
// 判定序（固定，可单测枚举全部路径）：
//   1. BOM：UTF-8 / UTF-16 LE / UTF-16 BE
//   2. 无 BOM：UTF-16 零字节奇偶探测（BOM 丢失的老文件，>30% 判定）
//   3. 严格 UTF-8 校验
//   4. GBK(CP936) 与 Big5(CP950) 评分（Win32 转换，错误字符计数），接近则告警
#pragma once

#include <QByteArray>
#include <QString>
#include <vector>

#include "diag.hpp"

namespace suture {

struct DecodeResult {
    QString text;           // 解码后文本（换行已归一为 '\n'）
    QString encodingName;   // 判定结果（诊断/界面显示）
    bool    certain = true; // false = 启发式判定有歧义，UI 提供手动覆盖
    std::vector<Diagnostic> diags;
};

// 字节流 → QString。任何输入都不抛异常；完全无法解码时 text 为空串并出 Error。
DecodeResult decodeToQString(const QByteArray& raw);

}  // namespace suture
