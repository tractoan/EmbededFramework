#include "services/param_cli.hpp"

#include <cstdlib>
#include <cstring>

namespace svc {

int ParamCli::on_command(int argc, char** argv, CommandOutput& out) {
    if (store_ == nullptr) return -1;
    const char* sub = argc >= 2 ? argv[1] : "list";

    if (std::strcmp(sub, "list") == 0) {
        std::int32_t v = 0;
        for (std::size_t i = 0; i < store_->count(); ++i) {
            const ParamDef* d = store_->at(i, &v);
            if (d == nullptr) break;
            out.print("  %-16s = %-7ld [%ld..%ld] %s\r\n", d->name, static_cast<long>(v), static_cast<long>(d->min),
                      static_cast<long>(d->max), d->unit ? d->unit : "");
        }
        return 0;
    }
    if (std::strcmp(sub, "get") == 0 && argc >= 3) {
        const ParamDef* d = store_->find(argv[2]);
        if (d == nullptr) {
            out.print("unknown param '%s'\r\n", argv[2]);
            return -1;
        }
        out.print("%s = %ld\r\n", d->name, static_cast<long>(store_->get_or(d->id, d->default_value)));
        return 0;
    }
    if (std::strcmp(sub, "set") == 0 && argc >= 4) {
        const ParamDef* d = store_->find(argv[2]);
        if (d == nullptr) {
            out.print("unknown param '%s'\r\n", argv[2]);
            return -1;
        }
        char* end = nullptr;
        const long v = std::strtol(argv[3], &end, 0);
        if (end == argv[3] || *end != '\0') {
            out.print("invalid number '%s'\r\n", argv[3]);
            return -1;
        }
        const core::Status st = store_->set(d->id, static_cast<std::int32_t>(v));
        out.print("%s\r\n", core::to_string(st));
        return core::is_ok(st) ? 0 : -1;
    }
    if (std::strcmp(sub, "save") == 0) {
        const core::Status st = store_->save();
        out.print("save: %s\r\n", core::to_string(st));
        return core::is_ok(st) ? 0 : -1;
    }
    if (std::strcmp(sub, "reset") == 0) {
        store_->reset_defaults();
        out.print("defaults restored (use 'param save' to persist)\r\n");
        return 0;
    }
    out.print("usage: param list | get <name> | set <name> <value> | save | reset\r\n");
    return -1;
}

}  // namespace svc
