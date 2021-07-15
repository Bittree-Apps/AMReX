#ifndef AMREX_BittreeFunct_H_
#define AMREX_BittreeFunct_H_

#include <AMReX_Config.H>
#include <AMReX.H>
#include <AMReX_Vector.H>
#include <AMReX_BoxArray.H>
#include <AMReX_DistributionMapping.H>

namespace amrex
{
class BittreeFunct
{
public:
    using bittree_funptr_t = void(*)(int lev, Real time, int& new_finest, BoxArray& new_grids, DistributionMapping& bittree_dmap);

    BittreeFunct (bittree_funptr_t bittree_callback) : bittree_func(bittree_callback) {}

    ~BittreeFunct () {}

    void operator() (int lev, Real time, int& new_finest, BoxArray& new_grids, DistributionMapping& bittree_dmap);

private:
    bittree_funptr_t bittree_func = nullptr;
};
}
#endif
