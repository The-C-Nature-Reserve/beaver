#include "beaver.h"
#define FLAGS "-g -Wall -Werror -Og"

module_t modules[] = {
    { .name = "example", .src = "example.c" },
};

uint32_t modules_len = sizeof(modules) / sizeof(*modules);

char* program[] = { "example", NULL };

int main(int argc, char** argv)
{
    auto_update(argv);

    if (argc == 1) {
        compile(program, FLAGS);
    } else if (strcmp(argv[1], "clean") == 0) {
        rm("build/*");
        rm("out");
    }
    return 0;
}
