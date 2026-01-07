//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/ui/container.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/ui/widget.hpp"

#include <memory>
#include <vector>

namespace viper::tui::ui
{
/// @brief Base container holding child widgets.
class Container : public Widget
{
  public:
    /// @brief Add child widget to container.
    void addChild(std::unique_ptr<Widget> child);

    void layout(const Rect &r) override;
    void paint(render::ScreenBuffer &sb) override;

  protected:
    virtual void layoutChildren() = 0;
    std::vector<std::unique_ptr<Widget>> children_{};
};

/// @brief Vertical stack container.
class VStack : public Container
{
  protected:
    void layoutChildren() override;
};

/// @brief Horizontal stack container.
class HStack : public Container
{
  protected:
    void layoutChildren() override;
};

} // namespace viper::tui::ui
