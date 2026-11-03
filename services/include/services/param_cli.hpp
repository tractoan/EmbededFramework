#pragma once
#include "services/command_service.hpp"
#include "services/param_store.hpp"

namespace svc {

// Lệnh "param": list | get <name> | set <name> <value> | save | reset
class ParamCli final : public ICommandHandler {
public:
    void init(ParamStore* store) { store_ = store; }
    int on_command(int argc, char** argv, CommandOutput& out) override;

private:
    ParamStore* store_ = nullptr;
};

}  // namespace svc
