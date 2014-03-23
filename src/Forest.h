#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _FOREST_H_INC_
#define _FOREST_H_INC_

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

    void append (const typename Tree<T>::Ptr &tree);
    bool remove (const typename Tree<T>::Ptr &tree);

    size_t count () const;
    bool empty () const;

    Tree<T>& get_tree (size_t index) throw (std::out_of_range);
    Tree<T>& operator[] (size_t index);

    void clear ();

    template<class charT, class Traits>
    void print (std::basic_ostream<charT, Traits>& os) const;

    template<class T>
    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const Forest<T> &forest)
    {
        forest.print (os);
        return os;
    }

};

//template<class T>
//std::ostream& operator<< (std::ostream &ostream, const Forest<T> &forest)
//{
//    forest.print (ostream);
//    return ostream;
//}



template<class T>
inline Forest<T>::Forest ()
{}

template<class T>
inline Forest<T>::~Forest () { clear (); }

template<class T>
inline typename Tree<T>::List Forest<T>::trees () const { return _trees; }

template<class T>
inline void Forest<T>::trees (const typename Tree<T>::List &trees) { _trees = trees; }

template<class T>
inline void Forest<T>::append (const typename Tree<T>::Ptr &tree)
{
    _trees.emplace_back (std::make_shared< Tree<T> > (*tree));
}

template<class T>
inline bool Forest<T>::remove (const typename Tree<T>::Ptr &tree)
{
    bool removed = false;

    typename Tree<T>::List::const_iterator itr =
        std::find_if (
        _trees.begin (),
        _trees.end (),
        [&] (const std::shared_ptr< Tree<T> > &x) { return (*x == *tree); });

    while (itr != _trees.end ())
    {
        itr = _trees.erase (itr);
        removed =  true;
    }

    return removed;
}

template<class T>
inline typename size_t Forest<T>::count () const { return _trees.size (); }

template<class T>
inline bool Forest<T>::empty () const { return (0 == count ()); }

template<class T>
inline Tree<T>& Forest<T>::get_tree (size_t index) throw (std::out_of_range)
{
    if (0 > index || index >= _trees.size ()) throw std::out_of_range ("index out of range");
    return *(_trees[index]);
}

template<class T>
inline Tree<T>& Forest<T>::operator[] (size_t index) { return get_tree (index); }

template<class T>
inline void Forest<T>::clear () { _trees.clear (); }

template<class T>
template<class charT, class Traits>
inline void Forest<T>::print (std::basic_ostream<charT, Traits>& os) const
{
    os << endl;
    if (empty ())
    {
        os << "<empty>";
    }
    else
    {
        const typename Tree<T>::List &tree = _trees;
        size_t tree_count   = tree.size ();
        size_t count        = 1;
        
        for (typename Tree<T>::List::const_iterator itr = tree.cbegin ();
            itr != tree.cend ();
            ++itr)
        {
            os << ">";
            os << *(*itr);
            if (count != tree_count) os << endl;

            ++count;
        }
    }
}

#endif // _FOREST_H_INC_
