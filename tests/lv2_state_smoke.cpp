#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

namespace {

struct Uris {
  std::unordered_map<std::string, LV2_URID> uriToId{};
  std::unordered_map<LV2_URID, std::string> idToUri{};
  LV2_URID next{1};
};

LV2_URID mapUri(LV2_URID_Map_Handle handle, const char* uri) {
  auto& uris = *static_cast<Uris*>(handle);
  const auto it = uris.uriToId.find(uri);
  if (it != uris.uriToId.end()) {
    return it->second;
  }

  const LV2_URID id = uris.next++;
  uris.uriToId.emplace(uri, id);
  uris.idToUri.emplace(id, uri);
  return id;
}

struct StoredProperty {
  LV2_URID key{0};
  LV2_URID type{0};
  uint32_t flags{0};
  std::vector<std::byte> value{};
};

LV2_State_Status storeState(LV2_State_Handle handle,
                            uint32_t key,
                            const void* value,
                            size_t size,
                            uint32_t type,
                            uint32_t flags) {
  auto& property = *static_cast<StoredProperty*>(handle);
  property.key = key;
  property.type = type;
  property.flags = flags;
  property.value.assign(static_cast<const std::byte*>(value),
                        static_cast<const std::byte*>(value) + size);
  return LV2_STATE_SUCCESS;
}

const void* retrieveState(LV2_State_Handle handle,
                          uint32_t key,
                          size_t* size,
                          uint32_t* type,
                          uint32_t* flags) {
  const auto& property = *static_cast<const StoredProperty*>(handle);
  if (property.key != key || property.value.empty()) {
    return nullptr;
  }

  if (size != nullptr) {
    *size = property.value.size();
  }
  if (type != nullptr) {
    *type = property.type;
  }
  if (flags != nullptr) {
    *flags = property.flags;
  }

  return property.value.data();
}

}  // namespace

extern "C" const LV2_Descriptor* lv2_descriptor(std::uint32_t index);

int main() {
  Uris uris;
  LV2_URID_Map map{
      .handle = &uris,
      .map = mapUri,
  };
  const LV2_Feature mapFeature{
      .URI = LV2_URID__map,
      .data = &map,
  };
  const LV2_Feature* features[] = {
      &mapFeature,
      nullptr,
  };

  const LV2_Descriptor* descriptor = lv2_descriptor(0);
  if (descriptor == nullptr) {
    std::cerr << "LV2 descriptor is missing\n";
    return 1;
  }

  const auto* state = static_cast<const LV2_State_Interface*>(descriptor->extension_data(LV2_STATE__interface));
  if (state == nullptr) {
    std::cerr << "LV2 state interface is missing\n";
    return 1;
  }

  LV2_Handle source = descriptor->instantiate(descriptor, 48000.0, nullptr, features);
  if (source == nullptr) {
    std::cerr << "Failed to instantiate source LV2 plugin\n";
    return 1;
  }

  float targetLevel = -14.0f;
  float truePeak = -1.5f;
  float maxGain = 18.0f;
  float freezeLevel = -42.0f;
  float inputLevel = -23.0f;
  float correctionHigh = 85.0f;
  float correctionLow = 55.0f;
  float corrMixMode = 2.0f;
  float meterMode = 2.0f;
  float meterReset = 1.0f;
  descriptor->connect_port(source, 4, &targetLevel);
  descriptor->connect_port(source, 5, &truePeak);
  descriptor->connect_port(source, 6, &maxGain);
  descriptor->connect_port(source, 7, &freezeLevel);
  descriptor->connect_port(source, 8, &inputLevel);
  descriptor->connect_port(source, 9, &correctionHigh);
  descriptor->connect_port(source, 10, &correctionLow);
  descriptor->connect_port(source, 11, &corrMixMode);
  descriptor->connect_port(source, 12, &meterMode);
  descriptor->connect_port(source, 13, &meterReset);

  StoredProperty storedA;
  if (state->save(source, storeState, &storedA, 0, features) != LV2_STATE_SUCCESS) {
    std::cerr << "Saving LV2 state failed\n";
    descriptor->cleanup(source);
    return 1;
  }
  descriptor->cleanup(source);

  if (storedA.value.empty()) {
    std::cerr << "Saved LV2 state blob is empty\n";
    return 1;
  }

  LV2_Handle restored = descriptor->instantiate(descriptor, 48000.0, nullptr, features);
  if (restored == nullptr) {
    std::cerr << "Failed to instantiate restored LV2 plugin\n";
    return 1;
  }

  if (state->restore(restored, retrieveState, &storedA, 0, features) != LV2_STATE_SUCCESS) {
    std::cerr << "Restoring LV2 state failed\n";
    descriptor->cleanup(restored);
    return 1;
  }

  StoredProperty storedB;
  if (state->save(restored, storeState, &storedB, 0, features) != LV2_STATE_SUCCESS) {
    std::cerr << "Saving restored LV2 state failed\n";
    descriptor->cleanup(restored);
    return 1;
  }
  descriptor->cleanup(restored);

  if (storedA.type != storedB.type || storedA.flags != storedB.flags || storedA.value != storedB.value) {
    std::cerr << "LV2 state blob did not roundtrip cleanly\n";
    return 1;
  }

  return 0;
}
