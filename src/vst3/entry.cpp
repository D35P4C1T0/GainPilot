#include "controller.hpp"
#include "plugin.hpp"
#include "common.hpp"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(gainpilot::vst3::kCompanyName, gainpilot::vst3::kCompanyWeb, gainpilot::vst3::kCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(gainpilot::vst3::kMonoProcessorCid),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GainPilot Mono",
           Vst::kDistributable,
           gainpilot::vst3::kPluginCategory,
           gainpilot::kVersionString,
           kVstVersionString,
           gainpilot::vst3::GainPilotPlugin<1>::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(gainpilot::vst3::kStereoProcessorCid),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GainPilot Stereo",
           Vst::kDistributable,
           gainpilot::vst3::kPluginCategory,
           gainpilot::kVersionString,
           kVstVersionString,
           gainpilot::vst3::GainPilotPlugin<2>::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(gainpilot::vst3::kControllerCid),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           "GainPilot Controller",
           0,
           "",
           gainpilot::kVersionString,
           kVstVersionString,
           gainpilot::vst3::GainPilotController::createInstance)

END_FACTORY
