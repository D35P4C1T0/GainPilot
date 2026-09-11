#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/atom/forge.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/worker/worker.h>

#include "gainpilot/state.hpp"

using gainpilot::ParamId;

static void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

using Node = std::unique_ptr<LilvNode, decltype(&lilv_node_free)>;
using State = std::unique_ptr<LilvState, decltype(&lilv_state_free)>;

struct Host {
  std::map<std::string, LV2_URID> urids;
  LV2_URID_Map map{this, [](LV2_URID_Map_Handle handle, const char* uri) {
    auto& ids = static_cast<Host*>(handle)->urids;
    return ids.emplace(uri, static_cast<LV2_URID>(ids.size() + 1)).first->second;
  }};
  LV2_URID id(const char* uri) { return map.map(map.handle, uri); }

  // No UI state messages are sent by these tests. Fail visibly if a new DSP
  // workflow requires worker execution instead of silently discarding work.
  unsigned workerRequests = 0;
  LV2_Worker_Schedule worker{this, [](LV2_Worker_Schedule_Handle handle, uint32_t, const void*) {
    ++static_cast<Host*>(handle)->workerRequests;
    return LV2_WORKER_ERR_UNKNOWN;
  }};
  int32_t maxBlock = 1024;
  LV2_Options_Option options[2]{
    {LV2_OPTIONS_INSTANCE, 0, id(LV2_BUF_SIZE__maxBlockLength), sizeof(maxBlock), id(LV2_ATOM__Int), &maxBlock},
    {}};
  LV2_Feature mapFeature{LV2_URID__map, &map};
  LV2_Feature workerFeature{LV2_WORKER__schedule, &worker};
  LV2_Feature optionsFeature{LV2_OPTIONS__options, options};
  const LV2_Feature* features[4]{&mapFeature, &workerFeature, &optionsFeature, nullptr};
};

struct Instance {
  Host& host;
  const LilvPlugin* plugin;
  std::unique_ptr<LilvInstance, decltype(&lilv_instance_free)> instance{nullptr, lilv_instance_free};
  std::map<std::string, uint32_t> ports;
  std::vector<float> controls;
  std::array<std::array<float, 1024>, 2> input{}, output{};
  alignas(8) std::array<uint8_t, 4096> eventsIn{}, eventsOut{};
  bool active = false;

  Instance(LilvWorld* world, const LilvPlugin* description, Host& owner, double rate, unsigned channels)
      : host(owner), plugin(description), controls(lilv_plugin_get_num_ports(plugin)) {
    instance.reset(lilv_plugin_instantiate(plugin, rate, host.features));
    require(instance != nullptr, "LV2 instantiation failed");
    const auto node = [&](const char* uri) { return Node(lilv_new_uri(world, uri), lilv_node_free); };
    const auto audio = node(LV2_CORE__AudioPort), control = node(LV2_CORE__ControlPort);
    const auto atom = node(LV2_ATOM__AtomPort), in = node(LV2_CORE__InputPort);
    unsigned inputs = 0, outputs = 0, atomInputs = 0, atomOutputs = 0;
    for (uint32_t index = 0; index < controls.size(); ++index) {
      const auto* port = lilv_plugin_get_port_by_index(plugin, index);
      ports.emplace(lilv_node_as_string(lilv_port_get_symbol(plugin, port)), index);
      const bool isInput = lilv_port_is_a(plugin, port, in.get());
      void* buffer = nullptr;
      if (lilv_port_is_a(plugin, port, audio.get())) {
        auto& count = isInput ? inputs : outputs;
        require(count < channels, "Too many audio ports");
        buffer = isInput ? input[count++].data() : output[count++].data();
      } else if (lilv_port_is_a(plugin, port, control.get())) {
        LilvNode* defaultValue = nullptr;
        lilv_port_get_range(plugin, port, &defaultValue, nullptr, nullptr);
        Node value(defaultValue, lilv_node_free);
        if (value) controls[index] = lilv_node_as_float(value.get());
        buffer = &controls[index];
      } else if (lilv_port_is_a(plugin, port, atom.get())) {
        ++(isInput ? atomInputs : atomOutputs);
        buffer = isInput ? eventsIn.data() : eventsOut.data();
      } else {
        throw std::runtime_error("Unsupported required port");
      }
      lilv_instance_connect_port(instance.get(), index, buffer);
    }
    require(inputs == channels && outputs == channels, "Incorrect mono/stereo audio layout");
    require(atomInputs == 1 && atomOutputs == 1, "Incorrect event port layout");
    for (size_t i = 0; i < gainpilot::kNumParameters; ++i) {
      const auto& spec = gainpilot::parameterSpec(static_cast<ParamId>(i));
      const auto found = ports.find(std::string(spec.key));
      require(found != ports.end(), "Missing parameter port: " + std::string(spec.key));
      const auto* port = lilv_plugin_get_port_by_index(plugin, found->second);
      require(lilv_port_is_a(plugin, port, control.get()) &&
              bool(lilv_port_is_a(plugin, port, in.get())) == !spec.outputOnly,
              "Incorrect parameter port direction: " + std::string(spec.key));
    }
  }

  ~Instance() { deactivate(); }
  float& value(ParamId id) { return controls.at(ports.at(std::string(gainpilot::parameterSpec(id).key))); }
  void activate() { lilv_instance_activate(instance.get()); active = true; }
  void deactivate() { if (active) lilv_instance_deactivate(instance.get()); active = false; }

  void run(uint32_t count, int64_t frame) {
    require(count <= 1024, "Oversized host block");
    LV2_Atom_Forge forge;
    lv2_atom_forge_init(&forge, &host.map);
    lv2_atom_forge_set_buffer(&forge, eventsIn.data(), eventsIn.size());
    LV2_Atom_Forge_Frame sequence, position;
    lv2_atom_forge_sequence_head(&forge, &sequence, 0);
    lv2_atom_forge_frame_time(&forge, 0);
    lv2_atom_forge_object(&forge, &position, 0, host.id(LV2_TIME__Position));
    lv2_atom_forge_key(&forge, host.id(LV2_TIME__frame));
    lv2_atom_forge_long(&forge, frame);
    lv2_atom_forge_key(&forge, host.id(LV2_TIME__speed));
    lv2_atom_forge_float(&forge, 1);
    lv2_atom_forge_pop(&forge, &position);
    lv2_atom_forge_pop(&forge, &sequence);
    auto* out = reinterpret_cast<LV2_Atom*>(eventsOut.data());
    out->size = eventsOut.size() - sizeof(LV2_Atom);
    out->type = host.id(LV2_ATOM__Chunk);
    lilv_instance_run(instance.get(), count);
    require(host.workerRequests == 0, "Unexpected worker request: host test needs worker support");
  }

  State save() {
    return State(lilv_state_new_from_instance(plugin, instance.get(), &host.map,
      nullptr, nullptr, nullptr, nullptr,
      [](const char* symbol, void* data, uint32_t* size, uint32_t* type) -> const void* {
        auto& self = *static_cast<Instance*>(data);
        // Output meters and Reset/Relearn are intentionally transient.
        for (const auto id : gainpilot::kStateParamIds) {
          if (gainpilot::parameterSpec(id).key == symbol) {
            *size = sizeof(float);
            *type = self.host.id(LV2_ATOM__Float);
            return &self.value(id);
          }
        }
        *size = *type = 0;
        return nullptr;
      }, this, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, host.features), lilv_state_free);
  }

  void restore(const LilvState* state) {
    lilv_state_restore(state, instance.get(),
      [](const char* symbol, void* data, const void* value, uint32_t size, uint32_t type) {
        auto& self = *static_cast<Instance*>(data);
        require(size == sizeof(float) && type == self.host.id(LV2_ATOM__Float), "Invalid restored control");
        std::memcpy(&self.controls.at(self.ports.at(symbol)), value, sizeof(float));
      }, this, 0, host.features);
  }
};

int main(int argc, char** argv) try {
  require(argc == 3, "Expected LV2 bundle path and channel count");
  const unsigned channels = static_cast<unsigned>(std::stoul(argv[2]));
  require(channels == 1 || channels == 2, "Invalid channel count");
  std::unique_ptr<LilvWorld, decltype(&lilv_world_free)> world(lilv_world_new(), lilv_world_free);
  require(world != nullptr, "Cannot create LV2 world");
  // Load exactly the requested bundle, never a stale system/user installation.
  const auto path = std::filesystem::absolute(argv[1]).string() + "/";
  Node bundle(lilv_new_file_uri(world.get(), nullptr, path.c_str()), lilv_node_free);
  lilv_world_load_bundle(world.get(), bundle.get());
  const auto* plugins = lilv_world_get_all_plugins(world.get());
  require(lilv_plugins_size(plugins) == 1, "Expected exactly one plugin in bundle");
  const auto* plugin = lilv_plugins_get(plugins, lilv_plugins_begin(plugins));
  require(lilv_plugin_verify(plugin), "Invalid LV2 metadata");
  const std::string uri = channels == 1 ? "https://gainpilot.dev/plugins/gainpilot-dpf-mono"
                                      : "https://gainpilot.dev/plugins/gainpilot-dpf-stereo";
  require(uri == lilv_node_as_uri(lilv_plugin_get_uri(plugin)), "Wrong plugin identity");

  for (const double rate : {44100., 48000., 96000.}) {
    for (const uint32_t block : {1u, 127u, 1024u}) {
      Host host;
      State saved(nullptr, lilv_state_free);
      std::array<float, gainpilot::kStateParamIds.size()> expected{};
      {
        Instance instance(world.get(), plugin, host, rate, channels);
        instance.value(ParamId::referenceMode) = 1;
        instance.value(ParamId::lockedReference) = -23;
        instance.value(ParamId::targetLevel) = -23;
        instance.value(ParamId::correctionHigh) = 0;
        instance.value(ParamId::correctionLow) = 0;
        instance.activate();
        const auto delay = static_cast<int64_t>(std::ceil(rate * .035375));
        int64_t frame = 0;
        while (frame < delay + block) {
          for (auto& input : instance.input) input.fill(0);
          if (frame == 0) { instance.input[0][0] = .1f; instance.input[1][0] = -.05f; }
          instance.run(block, frame);
          require(instance.controls.at(instance.ports.at("lv2_latency")) == delay, "Wrong reported latency");
          for (unsigned channel = 0; channel < channels; ++channel)
            for (uint32_t n = 0; n < block; ++n) {
              const float want = frame + n == delay ? (channel == 0 ? .1f : -.05f) : 0;
              require(std::abs(instance.output[channel][n] - want) < 1e-6f, "Impulse routing/delay mismatch");
            }
          frame += block;
        }
        instance.value(ParamId::targetLevel) = -14;
        instance.value(ParamId::lockedReference) = -29.25f;
        instance.run(block, frame);
        frame += block;
        require(instance.value(ParamId::inputReferenceValue) == -29.25f, "Locked-reference automation ignored");
        const auto reset = instance.value(ParamId::meterResetCount);
        instance.value(ParamId::meterReset) = 1;
        instance.run(block, frame);
        frame += block;
        require(instance.value(ParamId::meterResetCount) != reset, "Reset control ignored");
        instance.value(ParamId::meterReset) = 0;
        instance.run(block, frame);
        require(instance.value(ParamId::inputReferenceValue) == -29.25f, "Reset lost locked reference");
        const auto beforeRewind = instance.value(ParamId::meterResetCount);
        instance.run(block, 0);
        require(instance.value(ParamId::meterResetCount) != beforeRewind, "Transport rewind ignored");
        require(instance.value(ParamId::inputReferenceValue) == -29.25f, "Rewind lost locked reference");
        // Save while the trigger is high: a restored session must not replay it.
        instance.value(ParamId::meterReset) = 1;
        instance.run(block, block);
        instance.deactivate();
        saved = instance.save();
        require(saved && lilv_state_get_num_properties(saved.get()) > 0, "Missing plugin state blob");
        for (size_t i = 0; i < expected.size(); ++i) expected[i] = instance.value(gainpilot::kStateParamIds[i]);
      }
      Instance restored(world.get(), plugin, host, rate, channels);
      restored.restore(saved.get());
      restored.activate();
      restored.run(block, 0);
      for (size_t i = 0; i < expected.size(); ++i)
        require(restored.value(gainpilot::kStateParamIds[i]) == expected[i], "Persistent control changed on restore");
      require(restored.value(ParamId::inputReferenceValue) == -29.25f, "Restored DSP lost locked reference");
      require(restored.value(ParamId::meterReset) == 0, "Restore replayed transient reset");
      restored.deactivate();
      const auto again = restored.save();
      require(again && lilv_state_equals(saved.get(), again.get()), "Plugin blob/control state round trip differs");
      std::cout << "LV2 " << channels << "ch rate=" << rate << " block=" << block << " passed\n";
    }
  }
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
