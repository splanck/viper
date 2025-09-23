#pragma once
#include <string>
#include <string_view>

namespace viper::tui::term
{

class TermIO
{
  public:
    virtual ~TermIO() = default;
    virtual void write(std::string_view s) = 0;
    virtual void flush() = 0;
};

class RealTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;
    void flush() override;
};

class StringTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;

    void flush() override;

    const std::string &buffer() const;

    void clear();

  private:
    std::string buf_;
};

} // namespace viper::tui::term
