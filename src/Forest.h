//#pragma once
#ifndef FOREST_H_
#define FOREST_H_

#include "Tree.h"

#pragma warning (disable: 4290)

template<class T>
class Forest
{

private:
    typename Tree<T>::List _trees;


protected:

public:

    Forest ();
    ~Forest ();

    typename Tree<T>::List trees () const;
    void trees (const typename Tree<T>::List &trees);

    void appendTree (const typename Tree<T>::Ptr &tree);
    bool removeTree (const typename Tree<T>::Ptr &tree);

    size_t count () const;
    bool isempty () const;

    Tree<T>& getTree (size_t index) throw (std::out_of_range);
    Tree<T>& operator[] (size_t index);

    void clear ();

    template<class charT, class Traits>
    void print (::std::basic_ostream<charT, Traits>& os) const;

};

//template<class T>
//::std::ostream& operator<< (::std::ostream &ostream, const Forest<T> &forest)
//{
//    forest.print (ostream);
//    return ostream;
//}

template<class T>
template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const Forest<T> &forest)
{
    forest.print (os);
    return os;
}

#include "Forest.hpp"

#endif
