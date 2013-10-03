//#pragma once
#ifndef TREE_H_
#define TREE_H_

#include <memory>
#include <vector>
#include <stdexcept>

#pragma warning (disable: 4290)

template<class T>
class Tree
{

public:
    typedef Tree<T>* Ptr;

    typedef std::vector< std::shared_ptr< Tree<T> > >
        List;

private:
    typename T             _data;
    typename Tree<T>::Ptr  _root;
    typename Tree<T>::List _branches;


protected:

public:

    Tree (T data = T (), const typename Tree<T>::Ptr &root = NULL);
    ~Tree ();

    T&   data () const;
    void data (const T &data);

    typename Tree<T>::Ptr  root () const;
    void root (const typename Tree<T>::Ptr (&root));

    typename Tree<T>::List branches () const;
    void branches (const typename Tree<T>::List &branches);

    void appendBranch (T data);
    void appendBranch (const typename Tree<T>::Ptr &branch);

    bool removeBranch (T data);
    bool removeBranch (const typename Tree<T>::Ptr &branch);

    size_t count () const;
    bool isempty () const;
    size_t height () const;

    Tree<T>& getBranch (size_t index) throw (std::out_of_range);
    Tree<T>& operator[] (size_t index);

    bool operator== (const Tree<T> &tree) const;
    bool operator!= (const Tree<T> &tree) const;

    void clear ();

    template<class charT, class Traits>
    void print (::std::basic_ostream<charT, Traits>& os, size_t indent) const;

};

//template<class T>
//inline ::std::ostream& operator<< (std::ostream &ostream, const Tree<T> &tree)
//{
//    tree.print (ostream, 0);
//    return ostream;
//}

template<class T>
template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const Tree<T> &tree)
{
    tree.print (ostream, 0);
    return os;
}

#include "Tree.hpp"

#endif
