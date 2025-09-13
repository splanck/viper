// tui/include/tui/ui/modal.hpp
// @brief Modal host and popup widgets handling layered dialogs.
// @invariant ModalHost routes events to top modal; Popup centers within host.
// @ownership ModalHost owns root and modals; Popup borrows dismiss callback.
#pragma once

#include "tui/ui/widget.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace viper::tui::ui
{
class Popup;

/// @brief Hosts a root widget and manages a stack of modal widgets.
class ModalHost : public Widget
{
  public:
    /// @brief Construct host with root widget.
    explicit ModalHost(std::unique_ptr<Widget> root);

    /// @brief Push a modal widget on top.
    void pushModal(std::unique_ptr<Widget> modal);

    /// @brief Pop the topmost modal.
    void popModal();

    void layout(const Rect &r) override;
    void paint(render::ScreenBuffer &sb) override;
    bool onEvent(const Event &ev) override;

    /// @brief Always focusable to intercept events.
    [[nodiscard]] bool wantsFocus() const override
    {
        return true;
    }

    /// @brief Access underlying root widget.
    [[nodiscard]] Widget *root();

  private:
    std::unique_ptr<Widget> root_{};
    std::vector<std::unique_ptr<Widget>> modals_{};
};

/// @brief Simple popup widget with border and dismiss callback.
class Popup : public Widget
{
  public:
    /// @brief Construct popup with dimensions.
    Popup(int w, int h);

    /// @brief Set callback invoked on dismiss.
    void setOnClose(std::function<void()> cb);

    void layout(const Rect &r) override;
    void paint(render::ScreenBuffer &sb) override;
    bool onEvent(const Event &ev) override;

    [[nodiscard]] bool wantsFocus() const override
    {
        return true;
    }

  private:
    int width_{0};
    int height_{0};
    Rect box_{};
    std::function<void()> onClose_{};
};

} // namespace viper::tui::ui
