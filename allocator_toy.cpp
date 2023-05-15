// This code demonstrates wrapping a raw array in a std::vector
// This code is not for production, and may only be used for starting fights.
// Author: Brett Downing 2023
// License: BSD

#include <iostream>
#include <vector>
#include <functional>

// we're going to take or borrow memory, we need to track obligations.
// this struct contains the information that the user needs to free the memory
struct zc_loan {
    void* ptr;   // raw pointer to the data, searched by this index
    std::function<void(void*, size_t)> unref_cb; // callback that can clean up
};
// For gstreamer, this callback will capture the mapinfo that maps the gst_buffer


// create an allocator class that can track and manage borrowed memory
template<class T>
class zc_allocator  // note: not inheriting from : public std::allocator<T>
{
    public:
    typedef T value_type;

    zc_allocator () = default;

    // copy contstructors are permitted to change the type of the allocator
    template<class U>
    constexpr zc_allocator (const zc_allocator <U>&) noexcept {}

    // wrap allocate just so we can attach a print function
    // (we can delete this overload)
    [[nodiscard]] T* allocate(std::size_t n) {
        T* p = std_allocator.allocate(n);
        report(p,n,true);
        return p;
    }

    // check if the pointer we're disposing has any strings attached
    // This overload is necessary
    void deallocate(T* p, std::size_t n) noexcept {
        
        void* ptr = reinterpret_cast<void*>(p); // make this legible

        // test if p is a pointer that we've borrowed
        // XXX definitely mutex against re-entrant read-modify
        for(size_t i=0; i<borrowed_buffers.size(); i++) {
            if(ptr == borrowed_buffers[i].ptr){
                std::cout << "dropping wrapping on:" << std::hex
                    << std::showbase << ptr << std::dec << '\n';

                // do the user's cleanup
                borrowed_buffers[i].unref_cb(ptr, n);

                // forget the pointer
                borrowed_buffers[i] = borrowed_buffers.back();
                borrowed_buffers.pop_back();

                return;
            }
        }
        // no strings attached to p, use the default deallocate
        report(p, n, 0);
        std_allocator.deallocate(p,n);
    }

    // accept a buffer on loan
    // keeping track of what needs to happen when we're done.
    void wrap_buffer(
        T* p, std::size_t n,
        std::function<void(void*, size_t)> cb
    ) {
        std::cout << "wrapping:" << sizeof(T) * n
                  << " bytes at " << std::hex << std::showbase
                  << reinterpret_cast<void*>(p) << std::dec << '\n';

        // XXX mutex against concurrent writes, and concurrent read-write
        borrowed_buffers.push_back({reinterpret_cast<void*>(p), cb});
    }


private:
    // hold a std::allocator
    // this is instead of inheriting from std::allocator<T>
    // this way we can be sure we're intercepting all deacllocate calls.
    std::allocator<T> std_allocator;

    // declare the borrow list static so we can safely copy this allocator
    //   and still free pointers wrapped on other allocators
    static std::vector<zc_loan> borrowed_buffers;

    // trivial instrumentation
    void report(T* p, std::size_t n, bool alloc = true) const {
        std::cout << (alloc ? "Alloc:   " : "Dealloc: ") << sizeof(T) * n
                  << " bytes at " << std::hex << std::showbase
                  << reinterpret_cast<void*>(p) << std::dec << '\n';
    }
};

// declare one instance of the zc_allocator pointer store
template<>
std::vector<zc_loan> zc_allocator<uint8_t>::borrowed_buffers = std::vector<zc_loan>();


// all zc_allocators can de-allocate memory wrapped by other zc_allocators
template<class T, class U>
bool operator==(const zc_allocator <T>&, const zc_allocator <U>&) { return true; }
template<class T, class U>
bool operator!=(const zc_allocator <T>&, const zc_allocator <U>&) { return false; }


// Create a derived type of std::vector that can use the borrow tracking in the allocator
template<class T>
class zc_vector : public std::vector<T, zc_allocator<T> >
{
    public:
    void wrap_buffer(
        T* p,
        std::size_t n,
        std::function<void(void*, size_t)> cb
    ){
        // we're going to do pointer hacking, let's have one less pointer before we start.
        //this->clear();  // doesn't deallocate
        //this->resize(0);    // doesn't deallocate
        //this->shrink_to_fit();  // not strictly guaranteed to deallocate
        zc_vector<T>().swap(*this); // definitely deallocates

        // let the allocator know what's going on
        this->get_allocator().wrap_buffer(p, n, cb);

        // ##################### black magic starts here ######################
        // https://stackoverflow.com/questions/7278347/c-pointer-array-to-vector
        {   // GCC
            // access the implementation directly by first casting
            //   the zc_allocator `this` pointer to a void* pointer
            typename std::_Vector_base<T, zc_allocator<T> >::_Vector_impl *vectorPtr =
                (typename std::_Vector_base<T, zc_allocator<T> >::_Vector_impl *)((void *) this);
            // set the internal member variables of the implementation
            vectorPtr->_M_start = p;
            vectorPtr->_M_finish = vectorPtr->_M_end_of_storage = vectorPtr->_M_start + n;
        }
        /*
        {   // Microsoft Visual C++
            // access the implementation directly by first casting
            //   the zc_allocator `this` pointer to a void* pointer
            std::vector<T>::_Mybase* basePtr{ (std::vector<T>::_Mybase*)((void*) this) };
            // set the internal member variables of the implementation
            basePtr->_Get_data()._Myfirst = p;
            basePtr->_Get_data()._Mylast = basePtr->_Get_data()._Myend = basePtr->_Get_data()._Myfirst + n;
        }
        */
        // ######################## end of black magic ########################

        // XXX Eliminate the black magic by re-implementing vector
        //    so that we don't need to hack into its implementation
    }
};


int main()
{
    uint8_t buf[5] = {0,1,2,3,4};
    zc_vector<uint8_t> v;

    // first demonstrate that you can have a dirty vector
    std::cout << "resize" << '\n';
    v.resize(5);
    std::cout << "vector data is at" << std::hex << std::showbase << reinterpret_cast<void*>(v.data()) << std::dec << '\n';

    std::string cxt = "main context";
    
    std::cout << "wrap" << '\n';
    v.wrap_buffer(
        buf,
        sizeof(buf), 
        [=](void* ptr, size_t len){
            std::cout << "running unref lambda with captured context \"" << cxt << "\"" << '\n';
        }
    );
    
    // v and buf now point to the same memory
    v[0] = 6;
    buf[1] = 7;

    std::cout << "vector data is at" << std::hex << std::showbase << reinterpret_cast<void*>(v.data()) << std::dec << '\n';

    // if we try to increase the size, std::vector will re-allocate and move data
    std::cout << "push" << '\n';
    v.push_back(0);
    std::cout << "pop" << '\n';
    v.pop_back();
    std::cout << "vector data is at" << std::hex << std::showbase << reinterpret_cast<void*>(v.data()) << std::dec << '\n';

    // v and buf now point to different memory
    v[2] = 8;
    buf[3] = 9;

    std::cout << "buf looks like: ";
    for(int i=0; i<5; i++){std::cout << (int)buf[i] << " ";}
    std::cout << '\n';

    std::cout << "v looks like:   ";
    for(auto i: v){std::cout << (int)i << " ";}
    std::cout << '\n';

}
