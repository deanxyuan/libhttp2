/** @file settings.cc
 *  @brief Default parameter ranges for HTTP/2 SETTINGS (RFC 7540, Section 6.5.2).
 */
#include "http2/transport.h"

namespace http2 {

/** @brief Global table of valid ranges and defaults for all HTTP/2 SETTINGS parameters.
 *
 *  Each entry defines the setting name, default value, min/max range,
 *  the behavior when an out-of-range value is received (clamp or disconnect),
 *  and the error code to report.
 *
 *  Index 0 (Http2SettingsId::None) is a placeholder; indices 1-6 correspond
 *  to HeaderTableSize through MaxHeaderListSize.
 */
const SettingParameters global_settings_parameters[HTTP2_NUMBER_OF_SETTINGS] = {
    {"NULL", 0u, 0u, 0u, InvalidValueBehavior::Clamp, Http2ErrorCode::NoError},
    {"HEADER_TABLE_SIZE", 4096u, 0u, 4294967295u, InvalidValueBehavior::Clamp, Http2ErrorCode::ProtocolError},
    {"ENABLE_PUSH", 1u, 0u, 1u, InvalidValueBehavior::Disconnect, Http2ErrorCode::ProtocolError},
    {"MAX_CONCURRENT_STREAMS", 4294967295u, 0u, 4294967295u, InvalidValueBehavior::Disconnect, Http2ErrorCode::ProtocolError},
    {"INITIAL_WINDOW_SIZE", 65535u, 0u, 2147483647u, InvalidValueBehavior::Disconnect, Http2ErrorCode::FlowControlError},
    {"MAX_FRAME_SIZE", 16384u, 16384u, 16777215u, InvalidValueBehavior::Disconnect, Http2ErrorCode::ProtocolError},
    {"MAX_HEADER_LIST_SIZE", 16777216u, 0u, 16777216u, InvalidValueBehavior::Clamp, Http2ErrorCode::ProtocolError},
};
}  // namespace http2
