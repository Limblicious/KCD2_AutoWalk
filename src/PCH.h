#pragma once

// Minimal libKCD2/CryEngine prelude.
// Keep this intentionally narrower than libKCD2's kcd.h umbrella: AutoWalk needs
// the SDK/REL/RTTR foundation, not compile-time validation of every RE'd game class.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CryEngine/CryCommon/BaseTypes.h"
#include "CryEngine/CryCommon/CryString.h"
#include "CryEngine/CryCommon/Cry_Math.h"
#include "CryEngine/CryCommon/Cry_Geo.h"
#include "CryEngine/CryCommon/CryArray.h"

#include "Offsets/Offsets.h"
#include "Offsets/Offsets_VTABLE.h"
#include "Offsets/Offsets_RTTI.h"
#include "Offsets/RTTI.h"

#include "rttr/rttr_enable.h"
#include "rttr/string_view.h"
#include "rttr/detail/type_data.h"
#include "rttr/type.h"
#include "rttr/variant.h"
#include "rttr/argument.h"
#include "rttr/instance.h"
#include "rttr/detail/parameter_info_wrapper_base.h"
#include "rttr/parameter_info.h"
#include "rttr/parameter_info_iterator.h"
#include "rttr/parameter_info_range.h"
#include "rttr/detail/method_wrapper_base.h"
#include "rttr/detail/property_wrapper_base.h"
#include "rttr/property.h"
#include "rttr/method.h"
#include "rttr/constructor.h"
#include "rttr/destructor.h"
#include "rttr/detail/derived_info.h"
#include "rttr/detail/base_class_info.h"
#include "rttr/detail/class_data.h"
#include "rttr/detail/metadata.h"
#include "rttr/detail/enumeration_wrapper_base.h"
#include "rttr/enumeration.h"
