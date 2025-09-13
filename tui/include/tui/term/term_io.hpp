#pragma once
#include <string>
#include <string_view>

namespace tui::term
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
    void write(std::string_view s) override
    {
        buf_.append(s.data(), s.size());
    }

    void flush() override {}

    const std::string &buffer() const
    {
        return buf_;
    }

    void clear()
    {
        buf_.clear();
    }

  private:
    std::string buf_;
};

} // namespace tui::term
