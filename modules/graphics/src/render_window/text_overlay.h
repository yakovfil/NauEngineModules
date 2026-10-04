// Copyright 2026 N-GINN LLC. All rights reserved.
// Use of this source code is governed by a BSD-3 Clause license that can be found in the LICENSE file.
#pragma once

#include <memory>

#include "nau/utils/result.h"

namespace nau::render
{
    // Owned by a render window and destroyed before the Render driver closes.
    class TextOverlay
    {
    public:
        TextOverlay();
        ~TextOverlay();
        Result<> render(int width, int height);

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}  // namespace nau::render
