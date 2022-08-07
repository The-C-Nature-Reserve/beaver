#include "beaver.h"

#define FLAGS "-g -Wall -Werror -Og"

module_t modules[] = {
    { .name = "example", .src = "example.c", .extra_flags = "-lm" },
    { .name = "example", .module = "m1" },

    { .name = "m1", .src = "src/example.c", .extra_flags = "-lcurl" },

};
uint32_t modules_len = sizeof(modules) / sizeof(*modules);

char* program[] = { "example", "m1", NULL };

int main(int argc, char** argv)
{
    auto_update(argv);

    if (argc == 1) {
        compile(program, FLAGS);
        call_or_warn("./out");
    } else if (strcmp(argv[1], "clean") == 0) {
        rm("-r build/*");
        rm("out");
    }
    return 0;
}
