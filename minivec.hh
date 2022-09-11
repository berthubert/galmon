#pragma once
#include <math.h>
#include <iostream>

struct Point
{
  Point() : x{0}, y{0}, z{0}
  {}
  Point(double x_, double y_, double z_) : x(x_), y(y_), z(z_)
  {}
  double x, y, z;
};

inline std::ostream& operator<<(std::ostream& os, const Point& p)
{
  os << '(' << p.x << ", " << p.y << ", " << p.z <<')';
  return os;
}


struct Vector
{
  Vector() : x{0}, y{0}, z{0} {}
  Vector(double x_, double y_, double z_) : x(x_), y(y_), z(z_)
  {}
  Vector(const Point& a, const Point& b) : Vector(b.x - a.x, b.y - a.y, b.z - a.z)
  {
  }
  double x, y, z;

  double length() const
  {
    return sqrt(x*x + y*y + z*z);
  }
  
  void norm()
  {
    double l = length();
    x/=l;
    y/=l;
    z/=l;
  }

  double inner(const Vector& b) const
  {
    return x*b.x + y*b.y + z*b.z;
  }
};

inline std::ostream& operator<<(std::ostream& os, const Vector& p)
{
  os << '(' << p.x << ", " << p.y << ", " << p.z <<')';
  return os;
}

