#pragma once

#define DISTRHO_PLUGIN_BRAND "GainPilot"
#define DISTRHO_PLUGIN_NAME "GainPilot Mono"
#define DISTRHO_PLUGIN_LABEL "gainpilot_mono"
#define DISTRHO_PLUGIN_URI "https://gainpilot.dev/plugins/gainpilot-dpf-mono"
#define DISTRHO_PLUGIN_CLAP_ID "dev.gainpilot.gainpilot.mono"

#define DISTRHO_PLUGIN_BRAND_ID GnPl
#define DISTRHO_PLUGIN_UNIQUE_ID GnPm

#define DISTRHO_PLUGIN_HAS_UI 1
#define DISTRHO_PLUGIN_IS_RT_SAFE 1
#define DISTRHO_PLUGIN_NUM_INPUTS 1
#define DISTRHO_PLUGIN_NUM_OUTPUTS 1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_LATENCY 1
#define DISTRHO_PLUGIN_WANT_STATE 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS 1

#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:DynamicsPlugin"
#define DISTRHO_PLUGIN_LV2_STATE_PREFIX "https://gainpilot.dev/state/dpf/"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics|Mono"
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "dynamics", "mono"

#define DISTRHO_UI_DEFAULT_WIDTH 840
#define DISTRHO_UI_DEFAULT_HEIGHT 560
#define DISTRHO_UI_FILE_BROWSER 0
#define DISTRHO_UI_USER_RESIZABLE 1
#define DISTRHO_UI_USE_NANOVG 1
