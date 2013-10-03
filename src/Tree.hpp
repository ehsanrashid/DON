#include "xalgorithm.h"

template<class T>
Tree<T>::Tree (T data = T (), const typename Tree<T>::Ptr &root = NULL)
    : _data (data)
    , _root (root)
{}

template<class T>
Tree<T>::~Tree ()
{
    clear ();
}

template<class T>
T& Tree<T>::data () const
{
    return _data;
}
template<class T>
void Tree<T>::data (const T &data)
{
    _data = data;
}

template<class T>
typename Tree<T>::Ptr Tree<T>::root () const
{
    return _root;
}
template<class T>
void Tree<T>::root (const typename Tree<T>::Ptr (&root))
{
    _root = root;
}

template<class T>
typename Tree<T>::List Tree<T>::branches () const
{
    return _branches;
}
template<class T>
void Tree<T>::branches (const typename Tree<T>::List &branches)
{
    _branches = branches;
}

template<class T>
void Tree<T>::appendBranch (T data)
{
    std::shared_ptr< Tree<T> > spChild = std::make_shared< Tree<T> > (data, this);
    //spChild->_root = this;
    _branches.push_back (spChild);
}
template<class T>
void Tree<T>::appendBranch (const typename Tree<T>::Ptr &branch)
{
    if (branch->_root)
    {
        branch->_root->removeBranch (branch);
    }
    std::shared_ptr< Tree<T> > spBranch = std::make_shared< Tree<T> > (*branch);
    if (find (input_seq (_branches), spBranch) != _branches.end ()) return;

    spBranch->_root = this;
    _branches.push_back (spBranch);
}

template<class T>
bool Tree<T>::removeBranch (T data)
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
bool Tree<T>::removeBranch (const typename Tree<T>::Ptr &branch)
{
    bool removed = false;

    //typename Tree<T>::List::const_iterator itr = _branches.begin();
    //while (itr != _branches.end())
    //{
    //    if (*branch == *(*itr)) 
    //    {
    //        itr = _branches.erase(itr);
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
size_t Tree<T>::count () const
{
    return _branches.size ();
}
template<class T>
// leaf nodes are empty
bool Tree<T>::isempty () const
{
    return (0 == count ());
}
template<class T>
size_t Tree<T>::height () const
{
    return 0;
}


template<class T>
Tree<T>& Tree<T>::getBranch (size_t index) throw (std::out_of_range)
{
    if (0 > index || index >= _branches.size ()) throw std::out_of_range ("index out of range");
    return *(_branches[index]);
}


template<class T>
Tree<T>& Tree<T>::operator[] (size_t index)
{
    return getBranch (index);
}

template<class T>
bool Tree<T>::operator== (const Tree<T> &tree) const
{
    return (/*_root == tree._root &&*/ _data == tree._data);
}
template<class T>
bool Tree<T>::operator!= (const Tree<T> &tree) const
{
    return (/*_root != tree._root ||*/ _data != tree._data);
}

template<class T>
void Tree<T>::clear ()
{
    _branches.clear ();
}

template<class T>
template<class charT, class Traits>
void Tree<T>::print (::std::basic_ostream<charT, Traits>& os, size_t indent) const
{

    //os << _data;
    //if (isempty())
    //{
    //    //os << "(empty)";
    //}
    //else
    //{
    //    List branches = _branches;
    //    size_t countBranch = branches.size();
    //    size_t numBranch   = 1;
    //    os << "(";
    //    typename Tree<T>::List::const_iterator itr = branches.cbegin();
    //    while (itr != branches.cend())
    //    {
    //        (*(*itr)).print(os, indent+1);
    //        if (numBranch < countBranch) os << " | ";
    //        ++numBranch;
    //        ++itr;
    //    }
    //    os << ")";
    //}

    os << _data << endl;
    if (isempty ())
    {
        //os << "(empty)";
    }
    else
    {
        const List &branches = _branches;
        typename Tree<T>::List::const_iterator itr = branches.cbegin ();
        while (itr != branches.cend ())
        {
            for (uint8_t i = 0; i < indent; ++i) os << "|  ";
            os << "|->";
            (*(*itr)).print (os, indent + 1);
            ++itr;
        }
    }

}

