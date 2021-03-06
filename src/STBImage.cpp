#include "STBImage.hpp"

STBImage::STBImage(STBImage&& i) noexcept : _data(i._data), _x(i._x), _y(i._y), _n(i._n) {
	i._data = nullptr;
	i._x = i._y = i._n = 0;
}

STBImage& STBImage::operator=(STBImage&& i) noexcept {
	_data = i._data;
	_x = i._x;
	_y = i._y;
	_n = i._n;
	i._data = nullptr;
	i._x = i._y = i._n = 0;
	return *this;
}
