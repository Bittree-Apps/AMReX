#ifndef AMREX_BittreeFunct_H_
#define AMREX_BittreeFunct_H_

#include <AMReX_Config.H>
#include <AMReX.H>

namespace amrex
{
class BittreeFunct
{
public:
    using bittree_funptr_t = void(*)(int bittree_flg);

    BittreeFunct (bittree_funptr_t bittree_callback) : bittree_func(bittree_callback) {}

    ~BittreeFunct () {}

    void operator() (int bittree_flg);

protected:
    bittree_funptr_t bittree_func = nullptr;
};
}
#endif
