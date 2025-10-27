// tui/src/syntax/rules.cpp
// @brief Implementation of regex-based syntax highlighting with tiny JSON loader.
// @invariant Regex rules apply independently per line; cached spans reflect current line text.
// @ownership SyntaxRuleSet owns compiled regexes and cached spans.

#include "tui/syntax/rules.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace viper::tui::syntax
{
namespace
{
struct JsonParser
{
    const std::string &s;
    std::size_t i{0};

    void skipWs()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }

    bool expect(char c)
    {
        skipWs();
        if (i < s.size() && s[i] == c)
        {
            ++i;
            return true;
        }
        return false;
    }

    std::string parseString()
    {
        skipWs();
        std::string out;
        if (i >= s.size() || s[i] != '"')
            return out;
        ++i;
        while (i < s.size())
        {
            char c = s[i++];
            if (c == '"')
                break;
            if (c == '\\' && i < s.size())
            {
                char esc = s[i++];
                switch (esc)
                {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        out.push_back(esc);
                        break;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    bool parseBool()
    {
        skipWs();
        if (s.compare(i, 4, "true") == 0)
        {
            i += 4;
            return true;
        }
        if (s.compare(i, 5, "false") == 0)
        {
            i += 5;
            return false;
        }
        return false;
    }
};

render::RGBA parseColor(const std::string &hex)
{
    render::RGBA c{};
    if (hex.size() == 7 && hex[0] == '#')
    {
        c.r = static_cast<uint8_t>(std::stoi(hex.substr(1, 2), nullptr, 16));
        c.g = static_cast<uint8_t>(std::stoi(hex.substr(3, 2), nullptr, 16));
        c.b = static_cast<uint8_t>(std::stoi(hex.substr(5, 2), nullptr, 16));
    }
    return c;
}
} // namespace

bool SyntaxRuleSet::loadFromFile(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    JsonParser p{data};
    if (!p.expect('['))
        return false;
    while (true)
    {
        p.skipWs();
        if (p.i >= data.size())
            return false;
        if (data[p.i] == ']')
        {
            ++p.i;
            break;
        }
        if (!p.expect('{'))
            return false;
        std::string regexStr;
        render::Style style{};
        while (true)
        {
            p.skipWs();
            std::string key = p.parseString();
            if (key.empty())
                return false;
            if (!p.expect(':'))
                return false;
            if (key == "regex")
            {
                regexStr = p.parseString();
            }
            else if (key == "style")
            {
                if (!p.expect('{'))
                    return false;
                while (true)
                {
                    p.skipWs();
                    if (p.i >= data.size())
                        return false;
                    if (data[p.i] == '}')
                    {
                        ++p.i;
                        break;
                    }
                    std::string skey = p.parseString();
                    if (!p.expect(':'))
                        return false;
                    if (skey == "fg")
                    {
                        style.fg = parseColor(p.parseString());
                    }
                    else if (skey == "bold")
                    {
                        if (p.parseBool())
                            style.attrs |= render::Attr::Bold;
                    }
                    else
                    {
                        return false;
                    }
                    p.skipWs();
                    if (p.i >= data.size())
                        return false;
                    if (data[p.i] == ',')
                    {
                        ++p.i;
                        continue;
                    }
                    if (p.i >= data.size())
                        return false;
                    if (data[p.i] == '}')
                    {
                        ++p.i;
                        break;
                    }
                }
            }
            else
            {
                return false;
            }
            p.skipWs();
            if (p.i >= data.size())
                return false;
            if (data[p.i] == ',')
            {
                ++p.i;
                continue;
            }
            if (p.i >= data.size())
                return false;
            if (data[p.i] == '}')
            {
                ++p.i;
                break;
            }
        }
        if (!regexStr.empty())
        {
            rules_.push_back(SyntaxRule{std::regex(regexStr), style});
        }
        p.skipWs();
        if (p.i >= data.size())
            return false;
        if (data[p.i] == ',')
        {
            ++p.i;
            continue;
        }
        if (p.i >= data.size())
            return false;
        if (data[p.i] == ']')
        {
            ++p.i;
            break;
        }
    }
    return true;
}

const std::vector<Span> &SyntaxRuleSet::spans(std::size_t lineNo, const std::string &line)
{
    auto it = cache_.find(lineNo);
    if (it != cache_.end() && it->second.first == line)
        return it->second.second;
    std::vector<Span> out;
    for (const auto &rule : rules_)
    {
        for (std::sregex_iterator m(line.begin(), line.end(), rule.pattern), e; m != e; ++m)
        {
            out.push_back(Span{static_cast<std::size_t>(m->position()),
                               static_cast<std::size_t>(m->length()),
                               rule.style});
        }
    }
    cache_[lineNo] = {line, out};
    return cache_[lineNo].second;
}

void SyntaxRuleSet::invalidate(std::size_t lineNo)
{
    cache_.erase(lineNo);
}

} // namespace viper::tui::syntax
