// Offline Linux validation host. Reads/writes files only when explicitly run.
#include "travesty/audio_processor.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <ebur128.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sndfile.h>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

// These small host interfaces use the VST3 virtual method order, without SDK
// dependencies. Instances are owned by the host for the entire processing run.
struct Unknown {
  virtual v3_result query(const v3_tuid, void **result) {
    *result = nullptr;
    return V3_NO_INTERFACE;
  }
  virtual uint32_t ref() { return 1; }
  virtual uint32_t unref() { return 1; }
};
struct Queue : Unknown {
  v3_param_id id;
  double value;
  Queue(v3_param_id parameter, double initial)
      : id(parameter), value(initial) {}
  virtual v3_param_id parameter() { return id; }
  virtual int32_t count() { return 1; }
  virtual v3_result point(int32_t index, int32_t *offset, double *result) {
    if (index != 0)
      return V3_INVALID_ARG;
    *offset = 0;
    *result = value;
    return V3_OK;
  }
  virtual v3_result add(int32_t, double next, int32_t *index) {
    value = next;
    *index = 0;
    return V3_OK;
  }
};
struct Changes : Unknown {
  std::vector<std::unique_ptr<Queue>> queues;
  virtual int32_t count() { return static_cast<int32_t>(queues.size()); }
  virtual v3_param_value_queue **data(int32_t index) {
    return reinterpret_cast<v3_param_value_queue **>(queues.at(index).get());
  }
  virtual v3_param_value_queue **add(const v3_param_id *id, int32_t *index) {
    for (size_t i = 0; i < queues.size(); ++i)
      if (queues[i]->id == *id) {
        *index = static_cast<int32_t>(i);
        return data(*index);
      }
    *index = count();
    queues.push_back(std::make_unique<Queue>(*id, 0));
    return data(*index);
  }
};

int main(int argc, char **argv) try {
  require(argc >= 5 && argc <= 8,
          "Usage: gainpilot_vst3_render plugin.so input.wav output.wav "
          "target_LUFS [ceiling_dBTP=-9] [block=256] [offline=1]");
  require(!std::filesystem::exists(argv[3]),
          "Output already exists; choose a new file");
  const double target = std::stod(argv[4]);
  const double ceiling = argc > 5 ? std::stod(argv[5]) : -9;
  const int block = argc > 6 ? std::stoi(argv[6]) : 256;
  const int mode =
      argc > 7 && std::stoi(argv[7]) == 0 ? V3_REALTIME : V3_OFFLINE;
  require(target >= -30 && target <= -10 && ceiling >= -10 && ceiling <= 0 &&
              block > 0 && block <= 8192,
          "Invalid target, ceiling, or block size");
  SF_INFO info{};
  std::unique_ptr<SNDFILE, decltype(&sf_close)> source(
      sf_open(argv[2], SFM_READ, &info), sf_close);
  require(source && (info.channels == 1 || info.channels == 2),
          "Cannot read mono/stereo source audio");
  auto *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  require(library != nullptr, "Cannot load VST3 module");
  auto entry =
      reinterpret_cast<bool (*)(void *)>(dlsym(library, "ModuleEntry"));
  auto factoryFunction = reinterpret_cast<v3_plugin_factory **(*)()>(
      dlsym(library, "GetPluginFactory"));
  require(entry && factoryFunction && entry(library),
          "VST3 module entry failed");
  auto **factory = factoryFunction();
  v3_component **component = nullptr;
  for (int i = 0; i < v3_cpp_obj(factory)->num_classes(factory); ++i) {
    v3_class_info description{};
    require(v3_cpp_obj(factory)->get_class_info(factory, i, &description) ==
                V3_OK,
            "Cannot read VST3 class");
    if (std::strcmp(description.category, "Audio Module Class") == 0)
      require(v3_cpp_obj(factory)->create_instance(
                  factory, description.class_id, v3_component_iid,
                  reinterpret_cast<void **>(&component)) == V3_OK,
              "Cannot create VST3 component");
  }
  require(component && v3_cpp_obj_initialize(component, nullptr) == V3_OK,
          "Cannot initialize VST3 component");
  v3_audio_processor **processor = nullptr;
  require(v3_cpp_obj_query_interface(component, v3_audio_processor_iid,
                                     &processor) == V3_OK,
          "Missing audio processor");
  v3_tuid controllerId{};
  v3_edit_controller **controller = nullptr;
  require(v3_cpp_obj(component)->get_controller_class_id(
              component, controllerId) == V3_OK &&
              v3_cpp_obj(factory)->create_instance(
                  factory, controllerId, v3_edit_controller_iid,
                  reinterpret_cast<void **>(&controller)) == V3_OK &&
              v3_cpp_obj_initialize(controller, nullptr) == V3_OK,
          "Cannot initialize parameter controller");
  std::map<std::string, v3_param_id> ids;
  for (int i = 0; i < v3_cpp_obj(controller)->get_parameter_count(controller);
       ++i) {
    v3_param_info parameter{};
    require(
        v3_cpp_obj(controller)->get_parameter_info(controller, i, &parameter) ==
            V3_OK,
        "Cannot read parameter");
    std::string name;
    for (auto c : parameter.title) {
      if (!c)
        break;
      name += static_cast<char>(c);
    }
    ids.emplace(name, parameter.param_id);
  }
  Changes inputChanges, outputChanges;
  const auto set = [&](const char *name, double value) {
    const auto id = ids.at(name);
    inputChanges.queues.push_back(std::make_unique<Queue>(
        id, v3_cpp_obj(controller)
                ->plain_parameter_to_normalised(controller, id, value)));
  };
  set("Target Level", target);
  set("True Peak", ceiling);
  for (int direction : {V3_INPUT, V3_OUTPUT}) {
    v3_bus_info bus{};
    require(v3_cpp_obj(component)->get_bus_count(component, V3_AUDIO,
                                                 direction) == 1 &&
                v3_cpp_obj(component)->get_bus_info(
                    component, V3_AUDIO, direction, 0, &bus) == V3_OK &&
                bus.channel_count == info.channels,
            "Plugin/source channel layout mismatch");
    require(v3_cpp_obj(component)->activate_bus(component, V3_AUDIO, direction,
                                                0, true) == V3_OK,
            "Cannot activate bus");
  }
  v3_process_setup setup{mode, V3_SAMPLE_32, block,
                         static_cast<double>(info.samplerate)};
  require(v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK &&
              v3_cpp_obj(component)->set_active(component, true) == V3_OK &&
              v3_cpp_obj(processor)->set_processing(processor, true) == V3_OK,
          "Cannot start processing");
  const auto latency = v3_cpp_obj(processor)->get_latency_samples(processor);
  SF_INFO destinationInfo = info;
  destinationInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  std::unique_ptr<SNDFILE, decltype(&sf_close)> destination(
      sf_open(argv[3], SFM_WRITE, &destinationInfo), sf_close);
  require(destination != nullptr, "Cannot create output WAV");
  auto destroyMeter = [](ebur128_state *meter) { ebur128_destroy(&meter); };
  std::unique_ptr<ebur128_state, decltype(destroyMeter)> meter(
      ebur128_init(info.channels, info.samplerate,
                   EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK),
      destroyMeter);
  require(meter != nullptr, "Cannot create independent meter");
  std::vector<float> input(block * info.channels),
      interleaved(block * info.channels);
  std::vector<std::vector<float>> in(info.channels, std::vector<float>(block));
  auto out = in;
  std::vector<float *> inPointers, outPointers;
  for (int c = 0; c < info.channels; ++c) {
    inPointers.push_back(in[c].data());
    outPointers.push_back(out[c].data());
  }
  v3_audio_bus_buffers inBus{info.channels, 0, {inPointers.data()}},
      outBus{info.channels, 0, {outPointers.data()}};
  v3_process_context context{};
  context.state = V3_PROCESS_CTX_PLAYING;
  context.sample_rate = info.samplerate;
  double internal = -70;
  // Flush the full plugin delay, remove exactly that leading delay, and retain
  // the original frame count. Meter the exact float samples written to disk.
  for (sf_count_t position = 0; position < info.frames + latency;
       position += block) {
    const int count = static_cast<int>(
        std::min<sf_count_t>(block, info.frames + latency - position));
    std::fill(input.begin(), input.end(), 0);
    const auto wanted = std::min<sf_count_t>(
        count, std::max<sf_count_t>(0, info.frames - position));
    require(sf_readf_float(source.get(), input.data(), wanted) == wanted,
            "Source read failed");
    for (int n = 0; n < count; ++n)
      for (int c = 0; c < info.channels; ++c)
        in[c][n] = input[n * info.channels + c];
    context.project_time_in_samples = position;
    v3_process_data data{mode,
                         V3_SAMPLE_32,
                         count,
                         1,
                         1,
                         &inBus,
                         &outBus,
                         reinterpret_cast<v3_param_changes **>(&inputChanges),
                         reinterpret_cast<v3_param_changes **>(&outputChanges),
                         nullptr,
                         nullptr,
                         &context};
    require(v3_cpp_obj(processor)->process(processor, &data) == V3_OK,
            "VST3 processing failed");
    inputChanges.queues.clear();
    for (const auto &queue : outputChanges.queues)
      if (queue->id == ids.at("Output LUFS-I"))
        internal = v3_cpp_obj(controller)
                       ->normalised_parameter_to_plain(controller, queue->id,
                                                       queue->value);
    const int skip = static_cast<int>(std::min<sf_count_t>(
        count, std::max<sf_count_t>(0, latency - position)));
    for (int n = skip; n < count; ++n)
      for (int c = 0; c < info.channels; ++c) {
        require(std::isfinite(out[c][n]), "Non-finite rendered sample");
        interleaved[(n - skip) * info.channels + c] = out[c][n];
      }
    require(sf_writef_float(destination.get(), interleaved.data(),
                            count - skip) == count - skip &&
                ebur128_add_frames_float(meter.get(), interleaved.data(),
                                         count - skip) == 0,
            "Output write/meter failed");
  }
  std::vector<float> tail(512 * info.channels, 0);
  // Flush only the reference peak reconstruction; do not append to the WAV.
  require(ebur128_add_frames_float(meter.get(), tail.data(), 512) == 0,
          "Meter flush failed");
  double integrated = 0, peak = 0;
  require(ebur128_loudness_global(meter.get(), &integrated) == 0,
          "Loudness measurement failed");
  for (int c = 0; c < info.channels; ++c) {
    double p = 0;
    require(ebur128_true_peak(meter.get(), c, &p) == 0,
            "Peak measurement failed");
    peak = std::max(peak, p);
  }
  require(v3_cpp_obj(processor)->set_processing(processor, false) == V3_OK &&
              v3_cpp_obj(component)->set_active(component, false) == V3_OK,
          "Cannot stop VST3");
  v3_cpp_obj_terminate(controller);
  v3_cpp_obj_unref(controller);
  v3_cpp_obj_unref(processor);
  v3_cpp_obj_terminate(component);
  v3_cpp_obj_unref(component);
  v3_cpp_obj_unref(factory);
  auto exit = reinterpret_cast<bool (*)()>(dlsym(library, "ModuleExit"));
  require(exit && exit(), "VST3 module exit failed");
  dlclose(library);
  const double peakDb = 20 * std::log10(peak);
  std::cout << "target=" << target << " internal=" << internal
            << " external=" << integrated << " LUFS-I ceiling=" << ceiling
            << " peak=" << peakDb << " dBTP latency=" << latency
            << " block=" << block << " mode=" << mode << '\n';
  return peakDb <= ceiling && std::isfinite(integrated) ? 0 : 1;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
