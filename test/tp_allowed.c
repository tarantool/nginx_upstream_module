#include <tp_allowed_methods.h>
#include <stdio.h>
#include <string.h>

tp_allowed_methods_t tam;

void fill()
{
    char buf[1024];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "test %d", i);
        if (!tp_allowed_methods_add(&tam, (unsigned char *)buf, strlen(buf)))
            exit(1);
    }
}

void test()
{
    char buf[1024];
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "test %d", i);
        if (tp_allowed_methods_is_allow(&tam, (unsigned char*)buf, strlen(buf))) {
#if 0
            fprintf(stderr, "Matched %s\n", buf);
#endif
        } else
            exit(1);
    }
}

int main(int argc, char ** argv)
{
    tp_allowed_methods_init(&tam);
    fill();
    test();
    tp_allowed_methods_free(&tam);
    return 0;
}
