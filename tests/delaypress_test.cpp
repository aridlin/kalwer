#include "../delaypress_input.hpp"
#include "../launcher_commands.hpp"
#include <cassert>
int main() {
    using kalwer::delaypress::parse;
    auto request=parse("/delaypress enter 20s");
    assert(request && request->key.linux_code==KEY_ENTER && request->seconds==20);
    assert(parse("/delaypress Return 1S") && parse("/delaypress Space 86400"));
    for (auto command : {"/delaypress", "/delaypress enter", "/delaypress enter 0s",
         "/delaypress enter 3.9s", "/delaypress tab -2", "/delaypress enter 86401s",
         "/delaypress unknown 2s", "/delaypress enter 2s extra", "/delaypress enter 100000000000000s"})
        assert(!parse(command));
    auto commands=kalwer::matching_commands("/delaypress enter 20s");
    assert(commands.size()==1 && commands[0]->name=="/delaypress");
}
