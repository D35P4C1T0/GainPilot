#include "gainpilot/ui/wx_runtime.hpp"

#include <mutex>

#include <wx/init.h>

namespace gainpilot::ui {

namespace {

std::mutex gMutex;
int gRefCount = 0;

}  // namespace

bool acquireWxRuntime() {
  std::scoped_lock lock(gMutex);
  if (gRefCount == 0 && !wxInitialize()) {
    return false;
  }
  ++gRefCount;
  return true;
}

void releaseWxRuntime() {
  std::scoped_lock lock(gMutex);
  if (gRefCount == 0) {
    return;
  }

  --gRefCount;
  if (gRefCount == 0) {
    wxUninitialize();
  }
}

}  // namespace gainpilot::ui
