//#pragma once
#ifndef ALGORITHM_H_
#define ALGORITHM_H_

#include <algorithm>
#include <vector>

namespace std {

    template<class Itr>
    struct input_seq : public ::std::pair<Itr, Itr>
    {
        input_seq (Itr i1, Itr i2)
            : ::std::pair<Itr, Itr> (i1, i2)
        {}

    };

    template<class Container>
    inline input_seq<typename Container::const_iterator> mk_input_seq (Container &cont)
    {
        return input_seq<typename Container::const_iterator> (cont.begin (), cont.end ());
    }

    template<class Itr, class T>
    inline Itr find (input_seq<Itr> in_seq, const T &item)
    {
        return ::std::find (in_seq.first, in_seq.second, item);
    }

    template<class Itr, class Function>
    inline Itr for_each (input_seq<Itr> in_seq, Function func)
    {
        return ::std::for_each (in_seq.first, in_seq.second, func);
    }

    template<class Itr, class Predicate>
    inline Itr remove_if (input_seq<Itr> in_seq, Predicate pred)
    {
        return ::std::remove_if (in_seq.first, in_seq.second, pred);
    }

    // ---

    //template<typename T>
    //class sumer
    //{
    //private:
    //    T _sum;
    //
    //public:
    //    sumer (T sum = T ())
    //        : _sum (sum)
    //    {}
    //
    //    void operator() (const T &x)
    //    {
    //        _sum += x;
    //    }
    //
    //    T result () const
    //    {
    //        return _sum;
    //    }
    //
    //};

    //template<class Container>
    //inline typename Container::value_type sum (const Container &cont)
    //{
    //    auto sum = typename Container::value_type ();
    //
    //    typename Container::const_iterator itr  = cont.begin ();
    //    while (itr != cont.end ())
    //    {
    //        sum += *itr;
    //        ++itr;
    //    }
    //    return sum;
    //
    //    // ---
    //
    //    //sumer<class Container::value_type> sum;
    //    //std::for_each (mk_input_seq (cont), sum);
    //    //return sum.Result();
    //}


    // ---

    template<class Container>
    // Append the vectors
    inline void append (Container &cont1, const Container &cont2)
    {
        cont1.reserve (cont1.size () + cont2.size ());
        // -------------------------------------------------------------------
        //std::copy (cont2.cbegin (), cont2.cend (), ::std::back_inserter (cont1));
        // ---
        cont1.insert (cont1.cend (), cont2.cbegin (), cont2.cend ());
    }

    template<class Container>
    // Find min element
    inline typename Container::value_type find_min (const Container &cont)
    {
        return *(::std::min_element (cont.cbegin (), cont.cend (), ::std::less<typename Container::value_type> ()));
    }
    template<class Container>
    // Find max element
    inline typename Container::value_type find_max (const Container &cont)
    {
        return *(::std::max_element (cont.cbegin (), cont.cend (), ::std::less<typename Container::value_type> ()));
    }


    template<class Container>
    // Remove from vector by value
    inline Container& remove (Container &cont, const typename Container::value_type &val)
    {
        cont.erase (::std::remove (cont.begin (), cont.end (), val), cont.cend ());
        return cont;
    }
    template<class Container>
    // Remove from vector at n-th index
    inline void remove_at (Container &cont, size_t n)
    {
        // ---
        /// un-ordered delete
        //::std::swap(cont[n], cont.back());
        //cont.pop_back();
        // ---

        /// ordered delete
        //typename Container::const_iterator 
        auto itr = cont.cbegin ();
        ::std::advance (itr, n);
        cont.erase (itr);
        // -
        //cont.erase(cont.cbegin() + n);
    }


    template<class Container>
    inline size_t count (const Container &cont, const typename Container::value_type &item)
    {
        size_t count = 0;

        typename Container::const_iterator itr =
            ::std::find (cont.cbegin (), cont.cend (), item);
        //find (mk_input_seq (cont), item);
        while (itr != cont.cend ())
        {
            ++count;
            itr =::std::find (itr + 1, cont.cend (), item);
        }
        return count;
    }

    template<class Container>
    inline void print (const Container &cont)
    {
        //for (const typename Container::value_type &x : cont)
        for (auto &x : cont)
        {
            ::std::cout << x << ", ";
        }
        ::std::cout << ::std::endl;
    }


    template<class Container, class Predicate>
    inline void filter (Container &cont, Predicate pred)
    {
        cont.erase (::std::remove_if (cont.begin (), cont.end (), pred), cont.end ());
    }

    //template<class Container, class UnaryFunction>
    //inline void filter (Container &cont, UnaryFunction func)
    //{
    //    //typename Container::iterator
    //    auto
    //        itr = cont.begin ();
    //    while (itr != cont.end ())
    //    {
    //        //if (!func (itr))
    //        //{
    //        //    itr = cont.erase (itr); 
    //        //    continue;
    //        //}
    //        //++itr;
    //        itr = func (itr) ? (itr + 1) : cont.erase (itr);
    //    }
    //}

    template<class Container>
    // Clear(stack)
    inline void clear (Container &cont)
    {
        //typename Container::size_type size = cont.size ();
        //while (size--)
        //{
        //    cont.pop ();
        //}

        while (!cont.empty ())
        {
            cont.pop ();
        }
    }

    template<class T>
    inline void reverse_array (T *beg, T *end)
    {
        if (beg != end)
        {
            while (beg < end)
            {
                ::std::swap (*beg, *end);

                ++beg;
                if (beg == end) break;
                --end;
                if (beg == end) break;
            }
        }
    }

    template<class T>
    inline void reverse_array_stl_compliant (T *beg, T *end)
    {
        if (beg != end)
        {
            --end;
            if (beg != end)
            {
                while (beg < end)
                {
                    ::std::swap (*beg, *end);

                    ++beg;
                    if (beg == end) break;
                    --end;
                    if (beg == end) break;
                }
            }
        }

    }


}

#endif
