#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include <cassert>
#include <fstream>
#include <sstream>

int main()
{
    const char *files[] = {
        EXAMPLES_DIR "/il/ex1_hello_cond.il", EXAMPLES_DIR "/il/ex2_sum_1_to_10.il",
        EXAMPLES_DIR "/il/ex3_table_5x5.il",  EXAMPLES_DIR "/il/ex4_factorial.il",
        EXAMPLES_DIR "/il/ex5_strings.il",    EXAMPLES_DIR "/il/ex6_heap_array_avg.il"};
    for (const char *path : files)
    {
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        buf.seekg(0);
        il::core::Module m1;
        std::ostringstream err1;
        bool ok = il::io::Parser::parse(buf, m1, err1);
        assert(ok && err1.str().empty());
        std::string s1 = il::io::Serializer::toString(m1);
        std::istringstream in2(s1);
        il::core::Module m2;
        std::ostringstream err2;
        ok = il::io::Parser::parse(in2, m2, err2);
        assert(ok && err2.str().empty());
        std::string s2 = il::io::Serializer::toString(m2);
        if (!s1.empty() && s1.back() == '\n')
            s1.pop_back();
        if (!s2.empty() && s2.back() == '\n')
            s2.pop_back();
        assert(s1 == s2);
        std::ostringstream diag;
        assert(il::verify::Verifier::verify(m1, diag) && diag.str().empty());
    }
    return 0;
}
