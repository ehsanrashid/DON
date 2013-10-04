//#pragma once
#ifndef NON_COPYABLE_H_
#define NON_COPYABLE_H_

namespace std {

    class noncopyable
    {
    protected:
        noncopyable () {}
        ~noncopyable () {}

    private:  // emphasize the following members are private
        noncopyable (const noncopyable &);
        const noncopyable& operator= (const noncopyable &);
    };

}

#endif
