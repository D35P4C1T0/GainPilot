#pragma once

#include <string_view>

namespace gainpilot::vst3 {

inline constexpr std::string_view kVendor = "GainPilot contributors";
inline constexpr std::string_view kVendorUrl = "https://gainpilot.dev";
inline constexpr std::string_view kVendorEmail = "opensource@gainpilot.dev";

inline constexpr std::string_view kMonoProcessorCid = "c3a0b497-9c2b-44a0-9dbf-d07b9d9d7cf1";
inline constexpr std::string_view kMonoControllerCid = "a0a7779a-ea43-441d-ae4a-83563861346c";
inline constexpr std::string_view kStereoProcessorCid = "0a835034-00e0-4b33-874f-4b24fbe9bb10";
inline constexpr std::string_view kStereoControllerCid = "1dd495ea-45e5-4b92-aafe-7b4051eb89bc";

}  // namespace gainpilot::vst3
