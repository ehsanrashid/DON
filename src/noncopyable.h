//#pragma once
#ifndef NON_COPYABLE_H_
#define NON_COPYABLE_H_

namespace std {

    class noncopyable
    {
    
    protected:
        noncopyable () {}
        ~noncopyable () {}

        // Don't forget to declare these functions.
        // Want to make sure they are unaccessable & non-copyable
        // otherwise may accidently get copies of singleton.
        // Don't Implement these functions.
    
    private:  // emphasize the following members are private

        noncopyable (const noncopyable &);              // = delete;
        
        template<class T>
        T& operator= (const noncopyable &);
        //noncopyable& operator= (const noncopyable &); // = delete;

    };

}

#endif
