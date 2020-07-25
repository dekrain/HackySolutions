#include <stdio.h>
#include "../../cpp/Functional/Functional.hpp"

static constexpr int func(int a) noexcept
{
    return a;
}

int main()
{
    int b = 5;
    hsd::function f = func;  
    auto f2 = f;
    hsd::function f3 = hsd::bind(f, hsd::make_tuple(5));
    printf("%d\n", f3());
}