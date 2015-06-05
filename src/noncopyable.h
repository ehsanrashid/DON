#ifndef _NON_COPYABLE_H_INC_
#define _NON_COPYABLE_H_INC_

namespace std {

    class noncopyable
    {
    
    protected:
        noncopyable () = default;
       ~noncopyable () = default;

        // Don't forget to declare these functions.
        // Want to make sure they are unaccessable & non-copyable
        // otherwise may accidently get copies of singleton.
        // Don't Implement these functions.
    
    private:  // emphasize the following members are private
        
        template<class T>
        noncopyable (const T&) = delete;
        
        template<class T>
        T& operator= (const T&) { return T (); }
        
    };

}

#endif // _NON_COPYABLE_H_INC_
