// Copyright 2024 N-GINN LLC. All rights reserved.

// Copyright (C) 2024  Gaijin Games KFT.  All rights reserved
#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

#include "call_stack.h"

namespace drv3d_dx12::debug::break_point
{
namespace core
{
class Controller : public call_stack::ExecutionContextDataStore, public debug::call_stack::Reporter
{
    std::vector<std::string> breakPoints;

    using BaseType = call_stack::ExecutionContextDataStore;
    bool isBreakPoint(const call_stack::CommandData& call_stack_info)
    {
        if (breakPoints.empty())
        {
            return false;
        }
        auto resolved = resolve(call_stack_info);
        for (auto& breakPoint : breakPoints)
        {
            auto ref = std::search(resolved.begin(), resolved.end(), breakPoint.begin(), breakPoint.end());
            if (ref != resolved.end())
            {
                return true;
            }
        }
        return false;
    }

public:
  void breakNow() { __debugbreak(); }
  void setCommandData(const call_stack::CommandData &call_stack_info, const char *name)
  {
    BaseType::setCommandData(call_stack_info, name);
    if (isBreakPoint(call_stack_info))
    {
      breakNow();
    }
  }
  void addBreakPointString(std::string_view text)
  {
      breakPoints.emplace_back(text);
  }
  void removeBreakPointString(std::string_view text)
  {
      // Keep the lookup text alive while removing/moving owned breakpoint strings.
      std::string temp{text};
      auto newEnd = std::remove_if(begin(breakPoints), end(breakPoints), [temp](auto& bp)
      {
          return temp == bp;
      });
      breakPoints.erase(newEnd, end(breakPoints));
  }
};
} // namespace core
namespace null
{
class Controller : public call_stack::ExecutionContextDataStore, public debug::call_stack::Reporter
{
  using BaseType = call_stack::ExecutionContextDataStore;

public:
  void breakNow() {}
  void addBreakPointString(std::string_view)
  {
  }
  void removeBreakPointString(std::string_view)
  {
  }
};
} // namespace null

#if DAGOR_DBGLEVEL > 0
using Controller = ::drv3d_dx12::debug::break_point::core::Controller;
#else
using Controller = ::drv3d_dx12::debug::break_point::null::Controller;
#endif
} // namespace drv3d_dx12::debug::break_point