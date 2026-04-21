#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "gainpilot/parameters.hpp"
#include "gainpilot/ui/gtk_editor.hpp"

namespace {

constexpr int kWidth = 760;
constexpr int kHeight = 360;

void drainGtkEvents() {
  for (int i = 0; i < 8; ++i) {
    while (gtk_events_pending() != 0) {
      gtk_main_iteration();
    }
    g_usleep(20 * 1000);
  }
}

void setDemoState(gainpilot::ui::GainPilotGtkEditor& editor) {
  using gainpilot::ParamId;

  editor.setParameterValue(ParamId::targetLevel, -16.0f);
  editor.setParameterValue(ParamId::inputTrim, 1.5f);
  editor.setParameterValue(ParamId::truePeak, -1.0f);
  editor.setParameterValue(ParamId::maxGain, 12.0f);
  editor.setParameterValue(ParamId::programMode, static_cast<float>(gainpilot::ProgramMode::speech));
  editor.setParameterValue(ParamId::meterValue, -18.42f);
  editor.setParameterValue(ParamId::inputIntegratedValue, -21.87f);
  editor.setParameterValue(ParamId::outputIntegratedValue, -16.05f);
  editor.setParameterValue(ParamId::outputShortTermValue, -15.72f);
  editor.setParameterValue(ParamId::gainReductionValue, 4.8f);
  editor.setLatencyMilliseconds(12.5f);
}

}  // namespace

int main(int argc, char** argv) {
  if (!gainpilot::ui::ensureGtkUiRuntime()) {
    std::cerr << "Failed to initialize GTK. Run under a display server or xvfb-run.\n";
    return EXIT_FAILURE;
  }

  const std::filesystem::path outputPath = argc > 1 ? argv[1] : "docs/assets/gainpilot-ui.png";
  if (outputPath.has_parent_path()) {
    std::filesystem::create_directories(outputPath.parent_path());
  }

  auto* window = gtk_offscreen_window_new();
  {
    gainpilot::ui::GainPilotGtkEditor editor({}, "LV2 / GTK");
    setDemoState(editor);

    gtk_window_set_default_size(GTK_WINDOW(window), kWidth, kHeight);
    gtk_widget_set_size_request(window, kWidth, kHeight);
    gtk_widget_set_size_request(editor.widget(), kWidth, kHeight);
    gtk_container_add(GTK_CONTAINER(window), editor.widget());
    gtk_widget_show_all(window);
    gtk_widget_realize(window);
    drainGtkEvents();

    auto* pixbuf = gtk_offscreen_window_get_pixbuf(GTK_OFFSCREEN_WINDOW(window));
    if (pixbuf == nullptr) {
      std::cerr << "Failed to capture GTK offscreen window.\n";
      return EXIT_FAILURE;
    }
    if (gdk_pixbuf_get_width(pixbuf) < kWidth || gdk_pixbuf_get_height(pixbuf) < kHeight) {
      std::cerr << "Captured GTK snapshot has unexpected size: " << gdk_pixbuf_get_width(pixbuf) << "x"
                << gdk_pixbuf_get_height(pixbuf) << "\n";
      g_object_unref(pixbuf);
      return EXIT_FAILURE;
    }

    GError* error = nullptr;
    const auto saved = gdk_pixbuf_save(pixbuf, outputPath.string().c_str(), "png", &error, nullptr);
    g_object_unref(pixbuf);

    if (saved == 0) {
      std::cerr << "Failed to save " << outputPath << ": "
                << (error != nullptr ? error->message : "unknown error") << "\n";
      if (error != nullptr) {
        g_error_free(error);
      }
      return EXIT_FAILURE;
    }
  }

  gtk_widget_destroy(window);
  std::cout << "Wrote " << outputPath << "\n";
  return EXIT_SUCCESS;
}
