#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TREE_H_INC_
#define _TREE_H_INC_

#include <memory>
#include <vector>
#include <stdexcept>

#pragma warning (disable: 4290)

template<class T>
class Tree
{

public:
    typedef Tree<T>* Ptr;

    typedef std::vector<std::shared_ptr<Tree<T> > > List;

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

    void append (T data);
    void append (const typename Tree<T>::Ptr &branch);

    bool remove (T data);
    bool remove (const typename Tree<T>::Ptr &branch);

    size_t count () const;
    bool empty () const;
    size_t height () const;

    Tree<T>& get_branch (size_t index) throw (std::out_of_range);
    Tree<T>& operator[] (size_t index);

    bool operator== (const Tree<T> &tree) const;
    bool operator!= (const Tree<T> &tree) const;

    void clear ();

    template<class charT, class Traits>
    void print (std::basic_ostream<charT, Traits> &os, u32 indent) const;

};

//template<class T>
//inline std::ostream& operator<< (std::ostream &ostream, const Tree<T> &tree)
//{
//    tree.print (ostream, 0);
//    return ostream;
//}

template<class T>
template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Tree<T> &tree)
{
    tree.print (ostream, 0);
    return os;
}


#include <algorithm>
//#include "xalgorithm.h"

template<class T>
inline Tree<T>::Tree (T data = T (), const typename Tree<T>::Ptr &root = NULL)
    : _data (data)
    , _root (root)
{}

template<class T>
inline Tree<T>::~Tree () { clear (); }

template<class T>
inline T& Tree<T>::data () const { return _data; }
template<class T>
inline void Tree<T>::data (const T &data) { _data = data; }

template<class T>
inline typename Tree<T>::Ptr Tree<T>::root () const { return _root; }
template<class T>
inline void Tree<T>::root (const typename Tree<T>::Ptr (&root)) { _root = root; }

template<class T>
inline typename Tree<T>::List Tree<T>::branches () const { return _branches; }
template<class T>
inline void Tree<T>::branches (const typename Tree<T>::List &branches) { _branches = branches; }

template<class T>
inline void Tree<T>::append (T data)
{
    std::shared_ptr< Tree<T> > ptr_child = std::make_shared< Tree<T> > (data, this);
    //ptr_child->_root = this;
    _branches.emplace_back (ptr_child);
}
template<class T>
inline void Tree<T>::append (const typename Tree<T>::Ptr &branch)
{
    if (branch->_root)
    {
        branch->_root->remove (branch);
    }

    std::shared_ptr< Tree<T> > ptr_bran = std::make_shared< Tree<T> > (*branch);
    // TODO::
    //if (find (input_seq (_branches), ptr_bran) != _branches.end ()) return;

    ptr_bran->_root = this;
    _branches.emplace_back (ptr_bran);
}

template<class T>
inline bool Tree<T>::remove (T data)
{
    bool removed = false;
    typename Tree<T>::List::const_iterator itr = _branches.begin ();
    while (itr != _branches.end ())
    {
        if (data == (*itr)->_data)
        {
            itr = _branches.erase (itr);
            removed = true;
        }
        else
        {
            ++itr;
        }
    }

    //std::remove_if(_branches, std::bind(&Player::getpMoney, _1) <= 0 );

    return removed;
}
template<class T>
inline bool Tree<T>::remove (const typename Tree<T>::Ptr &branch)
{
    bool removed = false;

    //typename Tree<T>::List::const_iterator itr = _branches.begin ();
    //while (itr != _branches.end ())
    //{
    //    if (*branch == *(*itr)) 
    //    {
    //        itr = _branches.erase (itr);
    //        removed = true;
    //    }
    //    else
    //    {
    //        ++itr;
    //    }
    //}

    //branch->_root = this;

    typename Tree<T>::List::const_iterator itr =
        std::find_if (
        _branches.begin (),
        _branches.end (),
        [&] (const std::shared_ptr< Tree<T> > &x) { return (*x == *branch); });

    while (itr != _branches.end ())
    {
        itr = _branches.erase (itr);
        removed =  true;
    }
    return removed;
}

template<class T>
inline size_t Tree<T>::count () const { return _branches.size (); }
template<class T>
// leaf nodes are empty
inline bool Tree<T>::empty () const { return (0 == count ()); }
template<class T>
inline size_t Tree<T>::height () const { return 0; }

template<class T>
inline Tree<T>& Tree<T>::get_branch (size_t index) throw (std::out_of_range)
{
    if (0 > index || index >= _branches.size ()) throw std::out_of_range ("index out of range");
    return *(_branches[index]);
}

template<class T>
inline Tree<T>& Tree<T>::operator[] (size_t index) { return get_branch (index); }

template<class T>
inline bool Tree<T>::operator== (const Tree<T> &tree) const
{
    return (/*_root == tree._root &&*/ _data == tree._data);
}
template<class T>
inline bool Tree<T>::operator!= (const Tree<T> &tree) const
{
    return (/*_root != tree._root ||*/ _data != tree._data);
}

template<class T>
inline void Tree<T>::clear () { _branches.clear (); }

template<class T>
template<class charT, class Traits>
inline void Tree<T>::print (std::basic_ostream<charT, Traits> &os, u32 indent) const
{

    //os << _data;
    //if (empty())
    //{
    //    //os << "(empty)";
    //}
    //else
    //{
    //    List branches = _branches;
    //    size_t branch_count = branches.size();
    //    size_t count   = 1;
    //    os << "(";
    //    typename Tree<T>::List::const_iterator itr = branches.cbegin();
    //    while (itr != branches.cend())
    //    {
    //        (*(*itr)).print(os, indent+1);
    //        if (count < branch_count) os << " | ";
    //        ++count;
    //        ++itr;
    //    }
    //    os << ")";
    //}

    os << _data << endl;
    if (empty ())
    {
        //os << "(empty)";
    }
    else
    {
        const List &branches = _branches;
        typename Tree<T>::List::const_iterator itr = branches.cbegin ();
        while (itr != branches.cend ())
        {
            for (u32 i = 0; i < indent; ++i) os << "|  ";
            os << "|->";
            (*(*itr)).print (os, indent + 1);
            ++itr;
        }
    }

}

#endif // _TREE_H_INC_
