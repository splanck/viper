// File: tests/unit/test_vm_null_mem_ops.cpp
// Purpose: Verify VM traps when load/store operate on null pointers.
// Key invariants: Null pointer operands surface InvalidOperation traps with detail messages.
// Ownership: Standalone unit test executable.
// Links: docs/codemap.md

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

il::core::Module makeLoadModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr load;
    load.result = 0U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::null());
    load.loc = {1, 1, 1};
    bb.instructions.push_back(load);

    fn.blocks.push_back(std::move(bb));
    m.functions.push_back(std::move(fn));
    return m;
}

il::core::Module makeStoreModule()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands.push_back(Value::null());
    store.operands.push_back(Value::constInt(42));
    store.loc = {1, 2, 1};
    bb.instructions.push_back(store);

    fn.blocks.push_back(std::move(bb));
    m.functions.push_back(std::move(fn));
    return m;
}

std::string runModuleAndCapture(il::core::Module module)
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        il::vm::VM vm(module);
        vm.run();
        _exit(0);
    }

    close(fds[1]);
    char buf[512];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return std::string(buf);
}

} // namespace

int main()
{
    std::string loadTrap = runModuleAndCapture(makeLoadModule());
    bool loadOk = loadTrap.find("Trap @main#0 line 1: InvalidOperation (code=0): null load") != std::string::npos;
    assert(loadOk);

    std::string storeTrap = runModuleAndCapture(makeStoreModule());
    bool storeOk = storeTrap.find("Trap @main#0 line 2: InvalidOperation (code=0): null store") != std::string::npos;
    assert(storeOk);

    return 0;
}
