
template<class T>
Forest<T>::Forest ()
{}

template<class T>
Forest<T>::~Forest ()
{
    clear ();
}

template<class T>
typename Tree<T>::List Forest<T>::trees () const
{
    return _trees;
}

template<class T>
void Forest<T>::trees (const typename Tree<T>::List &trees)
{
    _trees = trees;
}

template<class T>
void Forest<T>::appendTree (const typename Tree<T>::Ptr &tree)
{
    _trees.push_back (std::make_shared< Tree<T> > (*tree));
}

template<class T>
bool Forest<T>::removeTree (const typename Tree<T>::Ptr &tree)
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
typename size_t Forest<T>::count () const
{
    return _trees.size ();
}

template<class T>
bool Forest<T>::isempty () const
{
    return (0 == count ());
}

template<class T>
Tree<T>& Forest<T>::getTree (size_t index) throw (std::out_of_range)
{
    if (0 > index || index >= _trees.size ()) throw std::out_of_range ("index out of range");
    return *(_trees[index]);
}

template<class T>
Tree<T>& Forest<T>::operator[] (size_t index)
{
    return getTree (index);
}

template<class T>
void Forest<T>::clear ()
{
    _trees.clear ();
}

template<class T>
template<class charT, class Traits>
void Forest<T>::print (::std::basic_ostream<charT, Traits>& os) const
{
    os << endl;
    if (isempty ())
    {
        os << "<empty>";
    }
    else
    {
        const typename Tree<T>::List &tree = _trees;
        size_t countTree = tree.size ();
        size_t numTree   = 1;
        typename Tree<T>::List::const_iterator itr = tree.cbegin ();
        while (itr != tree.cend ())
        {
            os << ">";
            os << *(*itr);
            if (numTree != countTree) os << endl;

            ++numTree;
            ++itr;
        }
    }
}

