# 每次构建重新生成 buildinfo.cpp（内容含时间戳，mtime 必变 → 必重编译）
# 目的：程序界面直接显示"构建于哪个提交+时间"，杜绝"跑的是旧 exe"的验证歧义
file(WRITE "${OUT}"
"// 自动生成：构建标识（勿手改，勿提交内容到版本库——文件在构建目录）
#include \"buildinfo.hpp\"

namespace suture {

const char* kBuildStamp = \"${STAMP}\";

}  // namespace suture
")
