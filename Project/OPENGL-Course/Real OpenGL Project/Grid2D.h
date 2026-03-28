#pragma once
#include <vector>

typedef unsigned int uint;

template<typename T>
class Grid2D
{
protected:
    uint _width;
    uint _height;
    uint _size;
    std::vector<T> _data;

public:
    Grid2D(uint w=0, uint h=0)
        : _width(w), _height(h), _size(w*h), _data(_size)
    {}

    uint width() const { return _width; }
    uint height() const { return _height;}
    uint size() const { return _size; }

    void resize(uint w, uint h)
    {
        _width = w;
        _height = h;
        _size = w*h;
        _data.resize(w*h);
    }

    void assign(T val)
    {
        _data.assign(_size, val);
    }

    T& operator ()(uint y, uint x);
    const T& operator ()(uint y, uint x) const;

    T& operator ()(uint i);
    const T& operator ()(uint i) const;

    T* ptr() { return &_data[0]; }
    const T* ptr() const { return &_data[0]; }
};

template<typename T>
T &Grid2D<T>::operator ()(uint y, uint x)
{
    // Clamp to prevent crashes (which were reported by the user occasionally if the math went out of bounds)
    y = y >= _height ? _height - 1 : y;
    x = x >= _width ? _width - 1 : x;
    return _data[y*_width+x];
}

template<typename T>
const T& Grid2D<T>::operator ()(uint y, uint x) const
{
    y = y >= _height ? _height - 1 : y;
    x = x >= _width ? _width - 1 : x;
    return _data[y*_width+x];
}

template<typename T>
T &Grid2D<T>::operator ()(uint i)
{
    return _data[i];
}

template<typename T>
const T& Grid2D<T>::operator ()(uint i) const
{
    return _data[i];
}
