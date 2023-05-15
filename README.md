# zero copy vector toy
C++ pointer hacking showing how to zero-copy wrap a raw buffer in a std::vector.

This uses a custom allocator that shims borrow and return functions ahead of a std::allocator,
and uses a derived type of std::vector that has a wrap method that does the necessary black magic.

I found this hard to find online, and there are plenty of use-cases where you want to reduce memory churn between C++ libraries.

Do read the code, there are more comments than code.

## Example output
```
$ g++ allocator_toy.cpp -O0 && ./a.out
resize
Alloc:   5 bytes at 0x5571b5ab92c0
vector data is at0x5571b5ab92c0
wrap
Dealloc: 5 bytes at 0x5571b5ab92c0
wrapping:5 bytes at 0x7fff54ffa293
vector data is at0x7fff54ffa293
push
Alloc:   10 bytes at 0x5571b5ab92c0
dropping wrapping on:0x7fff54ffa293
running unref lambda with captured context "main context"
pop
vector data is at0x5571b5ab92c0
buf looks like: 6 7 2 9 4 
v looks like:   6 7 8 3 4 
re-wrapping 
Dealloc: 10 bytes at 0x5571b5ab92c0
wrapping:5 bytes at 0x7fff54ffa293
dropping wrapping on:0x7fff54ffa293
running unref lambda with captured context "main context"
```

## Caveats:
The allocator remembers borrow operations using the pointer as the search key.\
This means that if you wrap a buffer twice, the wrong unref callback will be called when the vector re-allocates.\
it isn't a problem if the buffer is a reference-counted struct, or if the unref callback is a no-op.

