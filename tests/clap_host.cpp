#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "gainpilot/state.hpp"
// This executable consumes the entry point; it must not export it on Windows.
#define CLAP_EXPORT
#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/latency.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/thread-check.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using gainpilot::ParamId;
static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

struct Host {
  bool activating = false, audio = false, callback = false;
  unsigned notifications = 0, restarts = 0, invalidNotifications = 0;
  static Host& self(const clap_host_t* h) { return *static_cast<Host*>(h->host_data); }
  static const void* CLAP_ABI extension(const clap_host_t*, const char* id) {
    static const clap_host_latency_t latency{[](const clap_host_t* h) {
      auto& host = self(h);
      ++host.notifications;
      if (!host.activating || host.audio) ++host.invalidNotifications;
    }};
    static const clap_host_thread_check_t threads{
      [](const clap_host_t* h) { return !self(h).audio; },
      [](const clap_host_t* h) { return self(h).audio; }};
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latency;
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) return &threads;
    return nullptr;
  }
  clap_host_t api{CLAP_VERSION, this, "GainPilot regression host", "GainPilot", "", "1",
    extension, [](const clap_host_t* h) { ++self(h).restarts; },
    [](const clap_host_t*) {}, [](const clap_host_t* h) { self(h).callback = true; }};
  void poll(const clap_plugin_t* p) {
    if (callback) { callback = false; p->on_main_thread(p); }
    require(invalidNotifications == 0, "Latency notification outside activation");
  }
};

struct Events {
  std::vector<clap_event_param_value_t> values;
  clap_input_events_t input{this,
    [](const clap_input_events_t* e) { return static_cast<uint32_t>(static_cast<Events*>(e->ctx)->values.size()); },
    [](const clap_input_events_t* e, uint32_t i) -> const clap_event_header_t* { return &static_cast<Events*>(e->ctx)->values.at(i).header; }};
  clap_output_events_t output{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
  void add(ParamId id, double value, uint16_t space = 0, void* cookie = nullptr) {
    clap_event_param_value_t event{};
    event.header = {sizeof(event), 0, space, CLAP_EVENT_PARAM_VALUE, 0};
    event.param_id = static_cast<clap_id>(id);
    event.cookie = cookie;
    event.note_id = event.port_index = event.channel = event.key = -1;
    event.value = value;
    values.push_back(event);
  }
};

struct Stream {
  std::vector<char> bytes;
  size_t cursor = 0;
  clap_ostream_t out{this, [](const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t {
    auto& stream = *static_cast<Stream*>(s->ctx);
    const auto n = static_cast<size_t>(std::min<uint64_t>(7, size));
    const auto* first = static_cast<const char*>(data);
    stream.bytes.insert(stream.bytes.end(), first, first + n);
    return static_cast<int64_t>(n);
  }};
  clap_istream_t in{this, [](const clap_istream_t* s, void* data, uint64_t size) -> int64_t {
    auto& stream = *static_cast<Stream*>(s->ctx);
    const auto n = std::min<size_t>({7, static_cast<size_t>(size), stream.bytes.size() - stream.cursor});
    if (n) std::memcpy(data, stream.bytes.data() + stream.cursor, n);
    stream.cursor += n;
    return static_cast<int64_t>(n);
  }};
};

int main(int argc, char** argv) try {
  require(argc == 3, "Expected bundle path and channel count");
  const uint32_t channels = static_cast<uint32_t>(std::stoi(argv[2]));
  require(channels == 1 || channels == 2, "Invalid channels");
  const auto path = std::filesystem::absolute(argv[1]);
  auto binary = path;
#ifdef __APPLE__
  binary /= "Contents/MacOS";
  binary /= path.stem();
#endif
#ifdef _WIN32
  auto library = LoadLibraryW(binary.c_str());
  require(library != nullptr, "Cannot load CLAP library");
  auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(library, "clap_entry"));
#else
  auto* library = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
  require(library != nullptr, "Cannot load CLAP library");
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
#endif
  require(entry && entry->init(path.string().c_str()), "CLAP entry initialization failed");
  auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  require(factory && factory->get_plugin_count(factory) == 1, "Unexpected factory");
  Host host;
  const auto create = [&] {
    auto* p = factory->create_plugin(factory, &host.api, factory->get_plugin_descriptor(factory, 0)->id);
    require(p && p->init(p), "Cannot initialize plugin");
    return p;
  };
  const clap_plugin_t* plugin = create();
  const auto params = [&] { return static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS)); };
  const auto state = [&] { return static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE)); };
  const auto get = [&](ParamId id) {
    double value = 0;
    require(params()->get_value(plugin, static_cast<clap_id>(id), &value), "Parameter missing");
    return value;
  };
  const auto snapshot = [&] {
    std::vector<double> values;
    // Meter outputs and the momentary Reset trigger are intentionally transient.
    for (auto id : gainpilot::kStateParamIds) values.push_back(get(id));
    return values;
  };
  const auto flush = [&](Events& events) { params()->flush(plugin, &events.input, &events.output); host.poll(plugin); };
  require(params() && state(), "Missing params/state extension");
  auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
  require(ports != nullptr, "Missing audio ports");
  for (bool input : {true, false}) {
    clap_audio_port_info_t info{};
    require(ports->count(plugin, input) == 1 && ports->get(plugin, 0, input, &info) && info.channel_count == channels,
            "Incorrect mono/stereo port layout");
  }

  Events setup;
  setup.add(ParamId::referenceMode, 1);
  setup.add(ParamId::lockedReference, -23);
  setup.add(ParamId::targetLevel, -23);
  setup.add(ParamId::correctionHigh, 0);
  setup.add(ParamId::correctionLow, 0);
  flush(setup);
  std::vector<float> left(1024), right(1024), outLeft(1024), outRight(1024);
  float* inputs[]{left.data(), right.data()};
  float* outputs[]{outLeft.data(), outRight.data()};
  clap_audio_buffer_t in{inputs, nullptr, channels, 0, 0}, out{outputs, nullptr, channels, 0, 0};
  Events empty;
  int64_t frame = 0;
  const auto process = [&](uint32_t count, Events& events) {
    clap_process_t block{frame, count, nullptr, &in, &out, 1, 1, &events.input, &events.output};
    host.audio = true;
    require(plugin->process(plugin, &block) != CLAP_PROCESS_ERROR, "Processing failed");
    host.audio = false;
    frame += count;
    host.poll(plugin);
  };
  for (double rate : {44100., 48000., 96000.}) {
    for (uint32_t block : {1u, 127u, 1024u}) {
      host.activating = true;
      require(plugin->activate(plugin, rate, 1, block), "Activation failed");
      host.activating = false;
      auto* latency = static_cast<const clap_plugin_latency_t*>(plugin->get_extension(plugin, CLAP_EXT_LATENCY));
      const auto delay = static_cast<uint32_t>(std::ceil(rate * .035375));
      require(latency && latency->get(plugin) == delay, "Latency differs from configured delay");
      host.audio = true;
      require(plugin->start_processing(plugin), "Start failed");
      host.audio = false;
      frame = 0;
      while (frame < delay + block) {
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        if (frame == 0) { left[0] = .1f; right[0] = .05f; }
        const auto start = frame;
        process(block, empty);
        for (uint32_t n = 0; n < block; ++n) {
          require(std::abs(outLeft[n] - (start + n == delay ? .1f : 0)) < 1e-6f, "Left impulse routing/latency failed");
          if (channels == 2)
            require(std::abs(outRight[n] - (start + n == delay ? .05f : 0)) < 1e-6f, "Right impulse routing/latency failed");
        }
      }
      Events automated;
      automated.add(ParamId::targetLevel, -14);
      automated.add(ParamId::lockedReference, -29.25);
      process(block, automated);
      const auto expected = snapshot();
      Events wrong;
      wrong.add(ParamId::targetLevel, -30, 0xb33f);
      process(block, wrong);
      require(snapshot() == expected, "Foreign event namespace altered persistent parameters");
      const auto resetBefore = get(ParamId::meterResetCount);
      Events pulse;
      pulse.add(ParamId::meterReset, 1);
      pulse.add(ParamId::meterReset, 0);
      process(block, pulse);
      require(get(ParamId::meterResetCount) != resetBefore, "Short reset pulse lost");
      host.audio = true;
      plugin->stop_processing(plugin);
      host.audio = false;
      plugin->deactivate(plugin);
      flush(setup);
      flush(automated);
      require(snapshot() == expected, "Flush and process disagree on persistent values");
      flush(setup);
    }
  }
  Events locked;
  locked.add(ParamId::lockedReference, -29.25);
  locked.add(ParamId::targetLevel, -14);
  flush(locked);
  const auto saved = snapshot();
  Stream stream;
  require(state()->save(plugin, &stream.out), "Short-write state save failed");
  plugin->destroy(plugin);
  plugin = create();
  require(state()->load(plugin, &stream.in), "Short-read state load failed");
  const auto restored = snapshot();
  for (size_t i = 0; i < saved.size(); ++i) {
    if (std::abs(restored[i] - saved[i]) > 1e-5)
      std::cerr << "State parameter " << static_cast<unsigned>(gainpilot::kStateParamIds[i])
                << ": saved=" << saved[i] << " restored=" << restored[i] << '\n';
    require(std::abs(restored[i] - saved[i]) <= 1e-5, "Persistent state changed after instance recreation");
  }
  Stream again;
  require(state()->save(plugin, &again.out) && again.bytes == stream.bytes, "State binary round trip failed");
  Stream truncated;
  truncated.bytes.assign(stream.bytes.begin(), stream.bytes.end() - 2);
  require(!state()->load(plugin, &truncated.in), "Truncated state accepted");
  Stream garbage;
  garbage.bytes.assign(512, 'x');
  require(!state()->load(plugin, &garbage.in), "Unframed random state accepted");
  host.poll(plugin);
  require(host.notifications == 3 && host.restarts == 0, "Unexpected latency notification/restart count");
  plugin->destroy(plugin);
  entry->deinit();
#ifdef _WIN32
  FreeLibrary(library);
#else
  dlclose(library);
#endif
  std::cout << "CLAP " << channels << "ch: routing, latency lifecycle, automation, reset and state passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
