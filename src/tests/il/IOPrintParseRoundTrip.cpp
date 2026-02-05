//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/IOPrintParseRoundTrip.cpp
// Purpose: Ensure IL printer/parser round-trip stays stable across fixture corpus.
// Key invariants: Canonicalized serializer output must match after two parse/print cycles.
// Ownership/Lifetime: Test-owned modules/streams; files read from disk fixtures.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"
#include "viper/il/IO.hpp"

#include "tests/TestHarness.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::string trimWhitespace(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos)
    {
        return std::string();
    }
    const auto end = text.find_last_not_of(" \t");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string normalizeAttributes(std::string line)
{
    std::size_t searchStart = 0;
    while (true)
    {
        const std::size_t open = line.find('[', searchStart);
        if (open == std::string::npos)
        {
            break;
        }
        const std::size_t close = line.find(']', open);
        if (close == std::string::npos)
        {
            break;
        }
        const std::string attrs = line.substr(open + 1, close - open - 1);
        std::vector<std::string> parts;
        std::size_t partStart = 0;
        while (partStart < attrs.size())
        {
            const std::size_t comma = attrs.find(',', partStart);
            const std::size_t length =
                comma == std::string::npos ? std::string::npos : comma - partStart;
            const std::string piece = trimWhitespace(attrs.substr(partStart, length));
            if (!piece.empty())
            {
                parts.push_back(piece);
            }
            if (comma == std::string::npos)
            {
                break;
            }
            partStart = comma + 1;
        }
        std::stable_sort(parts.begin(), parts.end());
        std::string joined;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i != 0)
            {
                joined.append(", ");
            }
            joined.append(parts[i]);
        }
        line.replace(open + 1, close - open - 1, joined);
        searchStart = close + 1;
    }
    return line;
}

std::string normalizeText(const std::string &text)
{
    std::string withoutCarriageReturns;
    withoutCarriageReturns.reserve(text.size());
    for (const char ch : text)
    {
        if (ch != '\r')
        {
            withoutCarriageReturns.push_back(ch);
        }
    }

    std::istringstream stream(withoutCarriageReturns);
    std::string result;
    std::string line;
    bool firstLine = true;
    while (std::getline(stream, line))
    {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        line = normalizeAttributes(std::move(line));
        if (!firstLine)
        {
            result.push_back('\n');
        }
        firstLine = false;
        result.append(line);
    }
    return result;
}

std::vector<std::string> splitFixtureDirs(const std::string &dirs)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : dirs)
    {
        if (ch == ';' || ch == '|')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::vector<std::filesystem::path> collectFixtureFiles()
{
#ifdef IL_FIXTURE_DIRS
    const auto dirs = splitFixtureDirs(IL_FIXTURE_DIRS);
#else
    const std::vector<std::string> dirs;
#endif

    std::vector<std::filesystem::path> ilFiles;
    std::error_code ec;
    for (const auto &dirStr : dirs)
    {
        if (dirStr.empty())
        {
            continue;
        }
        ec.clear();
        const std::filesystem::path dir(dirStr);
        if (!std::filesystem::exists(dir, ec))
        {
            std::cerr << "Fixture directory missing: " << dir << '\n';
            continue;
        }
        for (std::filesystem::recursive_directory_iterator
                 it(dir, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             it != end;
             it.increment(ec))
        {
            if (ec)
            {
                std::cerr << "Error iterating " << dir << ": " << ec.message() << '\n';
                ec.clear();
                break;
            }
            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
            if (it->path().extension() == ".il")
            {
                ilFiles.push_back(it->path());
            }
        }
    }
    std::sort(ilFiles.begin(), ilFiles.end());
    ilFiles.erase(std::unique(ilFiles.begin(), ilFiles.end()), ilFiles.end());
    return ilFiles;
}

void reportDiag(const il::support::Diag &diag)
{
    il::support::printDiag(diag, std::cerr);
}

bool shouldSkipFixture(const std::filesystem::path &path)
{
    static const std::vector<std::string> kSkipFiles = {"serializer_all_opcodes.il"};
    const auto filename = path.filename().string();
    return std::find(kSkipFiles.begin(), kSkipFiles.end(), filename) != kSkipFiles.end();
}

} // namespace

TEST(IL, IOPrintParseRoundTrip)
{
    const auto fixtures = collectFixtureFiles();
    ASSERT_FALSE(fixtures.empty());

    for (const auto &fixture : fixtures)
    {
        if (shouldSkipFixture(fixture))
        {
            continue;
        }
        std::ifstream input(fixture);
        ASSERT_TRUE(input);

        std::ostringstream buffer;
        buffer << input.rdbuf();
        const std::string originalText = buffer.str();
        il::core::Module initialModule;
        std::istringstream firstStream(originalText);
        const auto firstParse = il::api::v2::parse_text_expected(firstStream, initialModule);
        ASSERT_TRUE(firstParse);

        const std::string firstPrinted = il::io::Serializer::toString(initialModule);
        const std::string canonicalFirstPrint = normalizeText(firstPrinted);

        il::core::Module roundTripped;
        std::istringstream secondStream(firstPrinted);
        const auto secondParse = il::api::v2::parse_text_expected(secondStream, roundTripped);
        ASSERT_TRUE(secondParse);

        const std::string secondPrinted = il::io::Serializer::toString(roundTripped);
        const std::string canonicalSecondPrint = normalizeText(secondPrinted);

        ASSERT_EQ(canonicalFirstPrint, canonicalSecondPrint);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
