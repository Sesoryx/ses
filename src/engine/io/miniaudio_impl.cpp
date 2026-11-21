// The only MA_IMPLEMENTATION translation unit in the project (ROADMAP §2.2).
// ODR rule: MA_IMPLEMENTATION must not be defined anywhere else. Keep this TU
// out of unity builds via SKIP_UNITY_BUILD_INCLUSION (set in CMakeLists.txt).

#define MA_IMPLEMENTATION
#include <miniaudio.h>
